//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "preamble_forward_decls.hpp"

#include "WithGpuContext.hpp"

#include "TimelineSemaphore.hpp"

#include <optional>

// #define VVV_SYNC_DEBUG

#ifdef VVV_SYNC_DEBUG
#include <fstream>
#include <iostream>
#endif

/*
 * A synchronization solution with two design goals:
 * - works without building a DAG: This allows streaming/eager/immediate execution of tasks.
 * - resolve the reusability/modularity problem of binary semaphores: semaphores can only be signaled once. Thus,
 *   each consumer needs its own wait semaphore. As a consequence, each consumer of a GPU pass needs to be known before the producer-GPU-pass
 *   can be submitted.
 *   A single semaphore per producer would be sufficient if all consumers are submitted together, but that inhibits
 *   parallelization in consumers of consumers and inhibits modularity since consumers have to be somehow batched
 *   before submission.
 *
 * This solution is precise, it eliminates all race conditions within the task stream while maximizes parallelism, meaning no parallelization opportunities
 * are missed through conservative approximations.
 *
 * The solution uses a set of timeline semaphores. The worst algorithm would generate a timeline semaphore per
 * GPU pass (node in the DAG) and signal it once. The optimal algorithm needs N timeline semaphores, where N is
 * the number of the maximal number of concurrently/parallel executed instructions in the instruction stream.
 *
 * ## Understanding Timeline Semaphore Path Compression
 *
 * We can significantly reduce the number of required timeline semaphores.
 *
 * ## Understanding Timeline Semaphore Reuse
 *
 * After a join operation (a submit waits on multiple prior submits), all but one of the incoming timeline semaphores will
 * become unused in the schedule (while the other one is reused for the node joining the control flow). HOWEVER, it is not
 * valid to reuse these unused semaphores. This is, because we only observe that the semaphore is unused in the current
 * planing state, but we DO NOT KNOW if the planed schedule has already executed. This distinction is important since
 * naive reuse without observed execution may mark paths lower in the DAG as executed if a semaphore is reused for
 * a dispatch inserted further up in the graph.
 *
 * At this point, the promise to plan execution without a DAG is slightly a lie. We have a path compressed DAG,
 * with a node per timeline semaphore (max concurrently executing submits) and a single edge per node to encode
 * the implications. But this is only an optimization to allow for automatic semaphore reuse.
 *
 * For most cases, a sufficiently good reuse pattern can be derived by hand. For example, marking all submits as
 * resolved up to the final planing state of frame N after the fence of the swapchain synchronization is a cheap
 * workaround that does not require a DAG to encode implications at all.
 */

namespace vvv {

/// Checkpoints the current planing state / progress of the schedule.
/// This can be used for fast and cheap observation of execution states, which is required for semaphore reuse.
typedef std::vector<uint64_t> SemaphoreState;

/// @brief Something that the GPU and CPU can wait for completion. This includes some
/// progress in a commandlist, the completion or submission to the queue.
///
/// This is a lightweight way to build a dependency graph. The numbers given
/// to each node (`createWaitable`) can be seen as the breath first search number.\n\n
///
/// Note: the fields in this struct should be read as follows: `the `value`-th dispatch
/// since program start is performing work on the `stages` GPU resources`. This
/// statement makes sense since we have a single timeline semaphore for the whole
/// program. So, for example, one could read: `the 13th dispatch since program
/// start is performing work using vertex shaders and fragment shaders.` if
/// `value=13` and `stages=Vk::PipelineStageFlagBits::eVertexShader | Vk::PipelineStageFlagBits::eFragmentShader`.
//
// interop with binary semaphores makes probably most sense if we track the semaphore along with the
// amount of times it was signaled. that requires that all submissions were done through the `submit` abstraction
// of this class. Currently, the `submit` abstraction just simplifies the call signature and its usage is optional.
// I think that would make binary semaphores identical to timeline semaphores apart from the fact that they can only
// be signaled/used in one child.
struct Awaitable {
    size_t semaphoreId;

    vk::Semaphore semaphore;
    /// The GPU workload guarded by the awaitable has finished execution when the actual value of `semaphore` is greater than or equal to `value`.
    /// The GPU workload is still pending (it has not started execution or is still executing) when the actual value of the `semaphore` is smaller than `value`.
    uint64_t value;
    /// stages should contain all pipeline stages that are used in the dispatch corresponding to the waitable.
    /// For example, if the waitable is using compute only, set this to `vk::PipelineStageFlagBits::eCompute`.
    /// If you are unsure or want to debug, set this to `vk::PipelineStageFlagBits::eAllCommands`.
    vk::PipelineStageFlags stages;
    /// Tracks the scheduled timeline semaphores in the instruction stream to optimize timeline semaphore reuse.
    SemaphoreState predecessorPlaningState;

    /// this field is available if awaitable creation and submission were decoupled. The field is
    /// deleted after submission.
    std::optional<std::vector<std::shared_ptr<Awaitable>>> awaitBeforeExecution;

    [[nodiscard]] SemaphoreState getInclusivePlaningState() const {
        SemaphoreState planingState(predecessorPlaningState);
        if (semaphoreId >= planingState.size()) {
            planingState.resize(semaphoreId + 1, 0);
        }

        // Note: given the monotony invariant of the semaphore state, using max to select from the predecessor planing state and the current state is not necessary.
        planingState[semaphoreId] = value;

        return planingState;
    }
};

/// This is a hack to support swapchain integration until timeline semaphores are supported in the swapchain API.
/// Our approach here is, that we allow a binary semaphore to introduce additional dependency edges without
/// `class Synchronization` performing any tracking for this edge.
///
/// This preserves correctness, since an additional dependency edge will just introduce further serialization
/// of the parallel workload. Since the driver is allowed to do this at any time, the method is robust against
/// sequential execution of parallel workloads anyways.
///
/// However, since the edge is not tracked, we may introduce unncessary new timeline semaphores since we are
/// not aware of the serialization introduced by the dependency edge of the binary semaphore.
struct BinaryAwaitable {
    vk::Semaphore semaphore;
    vk::PipelineStageFlags stages;
};

typedef std::shared_ptr<Awaitable> AwaitableHandle;
typedef std::vector<AwaitableHandle> AwaitableList;

typedef std::shared_ptr<BinaryAwaitable> BinaryAwaitableHandle;
typedef std::vector<BinaryAwaitableHandle> BinaryAwaitableList;

#ifdef VVV_SYNC_DEBUG
namespace detail {
const std::vector<std::string> colors{
    "brown1",
    "aquamarine2",
    "cornflowerblue",
    "darkgreen",
    "darkgoldenrod1",
    "darksalmon",
    "dodgerblue3",
    "darkorchid2",
    "chartreuse1",
    "darkorange4",
    "dodgerblue4",
    "gold1",
};
}; // namespace detail
#endif

class Synchronization : virtual public WithGpuContext {
  public:
    Synchronization(GpuContextPtr ctx)
        : WithGpuContext(ctx)
#ifdef VVV_SYNC_DEBUG
          ,
          m_dotfile(dot_write_start("debug-sync.dot"))
#endif
    {
    }

    ~Synchronization() {
#ifdef VVV_SYNC_DEBUG
        dot_write_end();
#endif
        m_semaphore.clear();
        m_executionState.clear();
    }

#ifdef VVV_SYNC_DEBUG

    std::string dot_id(AwaitableHandle awaitable) const { return "S" + std::to_string(awaitable->semaphoreId) + "V" + std::to_string(awaitable->value); }

    std::ofstream dot_write_start(const std::string filename) {
        std::ofstream f;
        f.open(filename, std::ios::trunc);
        f << "digraph G {" << std::endl;
        return f;
    }

    void dot_write_end() const {
        m_dotfile << "}" << std::endl;
        m_dotfile.close();
    }

    void write_dot_node(AwaitableHandle awaitable) const {
        const auto id = dot_id(awaitable);

        m_dotfile << id << "[color=" << detail::colors.at(awaitable->semaphoreId) << ", label=\"" << id << " (" << instructionCounter << ")\", tooltip=\"exec is [";

        for (int j = 0; j < m_executionState.size(); ++j) {
            m_dotfile << "S" << j << "=" << m_executionState[j] << ", ";
        }

        m_dotfile << "]\\nplan is [";

        for (int j = 0; j < awaitable->predecessorPlaningState.size(); ++j) {
            m_dotfile << "S" << j << "=" << awaitable->predecessorPlaningState[j] << ", ";
        }

        m_dotfile << "]";

        m_dotfile << "\"];" << std::endl;
        instructionCounter++;
    };

    void write_dot_edges(AwaitableHandle *preds, size_t predsCount, AwaitableHandle awaitable) const {

        if (predsCount < 1) {
            return;
        }

        m_dotfile << "{";

        for (int j = 0; j < predsCount; ++j) {
            m_dotfile << dot_id(preds[j]);
            if (j != (predsCount - 1)) {
                m_dotfile << ", ";
            }
        }

        m_dotfile << "} -> " << dot_id(awaitable) << ";" << std::endl;
    };
#endif

    /// Create a new node in the dependency graph
    // General structure of these call signatures are [actual args] [ legacy shit (awaitBinaryBeforeExecution, signalBinarySemaphore, signalFence) ]
    AwaitableHandle submit(vk::CommandBuffer commandBuffer, vk::Queue queue = static_cast<vk::Queue>(nullptr), AwaitableList awaitBeforeExecution = {},
                           vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eAllCommands, BinaryAwaitableList awaitBinaryBeforeExecution = {}, vk::Semaphore *signalBinarySemaphore = nullptr,
                           vk::Fence *signalFence = nullptr);
    AwaitableHandle submit(vk::CommandBuffer commandBuffer, uint32_t queueFamilyIndex = 0, AwaitableList awaitables = {}, vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eAllCommands,
                           BinaryAwaitableList awaitBinaryBeforeExecution = {}, vk::Semaphore *signalBinarySemaphore = nullptr, vk::Fence *signalFence = nullptr);

    /// submit a command buffer with a precreated awaitable
    void submit(vk::CommandBuffer commandBuffer, AwaitableHandle dependencies, uint32_t queueFamilyIndex = 0, BinaryAwaitableList awaitBinaryBeforeExecution = {},
                vk::Semaphore *signalBinarySemaphore = nullptr, vk::Fence *signalFence = nullptr);
    void submit(vk::CommandBuffer commandBuffer, AwaitableHandle dependencies, vk::Queue queue = static_cast<vk::Queue>(nullptr), BinaryAwaitableList awaitBinaryBeforeExecution = {},
                vk::Semaphore *signalBinarySemaphore = nullptr, vk::Fence *signalFence = nullptr);

    void hostWaitOnDevice(AwaitableList waitables, uint64_t maxWaitNanos = UINT64_MAX);
    /// Create an awaitable that is resolved when all currently planned work is done.
    // AwaitableHandle createAwaitAll();

    /// Check whether the awaitable has already executed. This will explicitly query the driver for the execution state.
    ///
    /// @see getKnownExecutionState for a variant that does not query the driver and uses cached state instead.
    bool isAwaitableResolved(AwaitableHandle awaitable);

    uint64_t getKnownExecutionState(uint32_t semaphoreId) { return m_executionState[semaphoreId]; };
    uint64_t getKnownExecutionState(AwaitableHandle awaitable) { return m_executionState[awaitable->semaphoreId]; };

    /// Mark a waitable as resolved on the host.
    /// This can be used to delay work on the GPU until this function is called, colloquially known as 'kicking the GPU'.
    void hostSignal(AwaitableHandle waitable);

    void markWaitablesAsResolved(AwaitableList waitables);
    AwaitableHandle createAwaitable(AwaitableList predecessors, vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eAllCommands);

    SemaphoreState checkpointPlaningState() const;
    void setExecutionState(const SemaphoreState &executionState);
    void setExecutionState(uint32_t semaphoreId, uint64_t semaphoreValue);

    /// explicitly queries the driver for the execution state. Otherwise execution state is only tracked
    ///  implicitly, e.g. through calls to `hostWaitOnDevice` and others.
    void readExecutionState();

    void destroySynchronizationPrimitives();

  private:
#ifdef VVV_SYNC_DEBUG
    mutable std::ofstream m_dotfile;
    mutable uint32_t instructionCounter;
#endif

    AwaitableHandle createAwaitable_(AwaitableHandle *predecessors, size_t predecessorsSize, vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eAllCommands, bool persistPredecessors = false);
    void submit_(vk::CommandBuffer commandBuffer, AwaitableHandle commandBufferAwaitable, AwaitableHandle *predecessors, size_t predecessorsSize, vk::Queue queue,
                 BinaryAwaitableList awaitBinaryBeforeExecution, vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence);

    size_t createAnotherSemaphore();

    SemaphoreState m_executionState;
    std::vector<std::unique_ptr<TimelineSemaphore>> m_semaphore;
};
} // namespace vvv