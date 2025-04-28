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

#include <vvv/core/GpuContext.hpp>
#include <vvv/core/Synchronization.hpp>
#include <vvv/util/Logger.hpp>

namespace vvv {

AwaitableHandle Synchronization::submit(vk::CommandBuffer commandBuffer, vk::Queue queue, AwaitableList awaitBeforeExecution, vk::PipelineStageFlags stages,
                                        BinaryAwaitableList awaitBinaryBeforeExecution, vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence) {
    auto newAwaitable = createAwaitable_(awaitBeforeExecution.data(), awaitBeforeExecution.size(), stages);
    submit_(commandBuffer, newAwaitable, awaitBeforeExecution.data(), awaitBeforeExecution.size(), queue, awaitBinaryBeforeExecution, signalBinarySemaphore, signalFence);
    return newAwaitable;
}

AwaitableHandle Synchronization::submit(vk::CommandBuffer commandBuffer, uint32_t queueFamilyIndex, AwaitableList awaitables, vk::PipelineStageFlags stages,
                                        BinaryAwaitableList awaitBinaryBeforeExecution, vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence) {
    return submit(commandBuffer, getCtx()->getQueue(queueFamilyIndex), awaitables, stages, awaitBinaryBeforeExecution, signalBinarySemaphore, signalFence);
}

void Synchronization::submit(vk::CommandBuffer commandBuffer, AwaitableHandle commandBufferAwaitable, uint32_t queueFamilyIndex, BinaryAwaitableList awaitBinaryBeforeExecution,
                             vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence) {
    submit(commandBuffer, commandBufferAwaitable, getCtx()->getQueue(queueFamilyIndex), awaitBinaryBeforeExecution, signalBinarySemaphore, signalFence);
}

void Synchronization::submit(vk::CommandBuffer commandBuffer, AwaitableHandle commandBufferAwaitable, vk::Queue queue, BinaryAwaitableList awaitBinaryBeforeExecution,
                             vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence) {

    if (!commandBufferAwaitable->awaitBeforeExecution.has_value()) {
        throw std::runtime_error("trying to submit a preallocated awaitable that was already submitted.");
    }

    submit_(commandBuffer, commandBufferAwaitable, commandBufferAwaitable->awaitBeforeExecution->data(), commandBufferAwaitable->awaitBeforeExecution->size(), queue, awaitBinaryBeforeExecution,
            signalBinarySemaphore, signalFence);
    commandBufferAwaitable->awaitBeforeExecution.reset();
}

void Synchronization::submit_(vk::CommandBuffer commandBuffer, AwaitableHandle commandBufferAwaitable, AwaitableHandle *predecessors, size_t predecessorsSize, vk::Queue queue,
                              BinaryAwaitableList awaitBinaryBeforeExecution, vk::Semaphore *signalBinarySemaphore, vk::Fence *signalFence) {

    if (queue == static_cast<vk::Queue>(nullptr)) {
        queue = getCtx()->getQueue();
    }

    // we have to write an arbitrary value for binary semaphores. The driver will ignore this value.
    const uint64_t IGNORED_VALUE = 0;

    std::vector<uint64_t> waitValues;
    std::vector<vk::Semaphore> waitSemaphores;
    std::vector<vk::PipelineStageFlags> waitDstStageMask;
    std::vector<uint64_t> signalValues({commandBufferAwaitable->value});
    std::vector<vk::Semaphore> signalSemaphores({commandBufferAwaitable->semaphore});

    if (signalBinarySemaphore != nullptr) {
        signalSemaphores.push_back(*signalBinarySemaphore);
        signalValues.push_back(IGNORED_VALUE);
    }

    for (int j = 0; j < predecessorsSize; ++j) {
        waitValues.push_back(predecessors[j]->value);
        waitSemaphores.push_back(predecessors[j]->semaphore);
        waitDstStageMask.push_back(predecessors[j]->stages);
    }

    for (const auto binaryDependence : awaitBinaryBeforeExecution) {
        waitValues.push_back(IGNORED_VALUE);
        waitSemaphores.push_back(binaryDependence->semaphore);
        waitDstStageMask.push_back(binaryDependence->stages);
    }

    vk::TimelineSemaphoreSubmitInfo timelineInfo(waitValues, signalValues);

    vk::SubmitInfo submitInfo(waitSemaphores, waitDstStageMask, commandBuffer, signalSemaphores);

    submitInfo.pNext = &timelineInfo;

    if (signalFence != nullptr) {
        queue.submit(submitInfo, *signalFence);
    } else {
        queue.submit(submitInfo);
    }

    // device().waitIdle();
}

void Synchronization::readExecutionState() {
    for (int j = 0; j < m_semaphore.size(); ++j) {
        const auto value = device().getSemaphoreCounterValue(m_semaphore[j]->getHandle());
        setExecutionState(j, value);
    }
}

bool Synchronization::isAwaitableResolved(AwaitableHandle awaitable) {

    // first check if we already know enough without asking the driver
    if (m_executionState[awaitable->semaphoreId] >= awaitable->value) {
        return true;
    }

    const auto value = device().getSemaphoreCounterValue(awaitable->semaphore);
    const auto isResolved = value >= awaitable->value;

    if (isResolved) {
        // mark the predecessors as executed
        setExecutionState(awaitable->predecessorPlaningState);
        // mark the awaitable itself as executed
        setExecutionState(awaitable->semaphoreId, awaitable->value);
    }

    return isResolved;
}

void Synchronization::hostWaitOnDevice(AwaitableList awaitables, uint64_t maxWaitNanos) {

    if (awaitables.empty()) {
        return;
    }

    std::vector<uint64_t> values;
    std::vector<vk::Semaphore> semaphores;

    for (const auto &awaitable : awaitables) {
        values.push_back(awaitable->value);
        semaphores.push_back(awaitable->semaphore);
    }

    vk::SemaphoreWaitInfo wait_info;
    wait_info.semaphoreCount = semaphores.size();
    wait_info.pSemaphores = semaphores.data();
    wait_info.pValues = values.data();
    const auto result = device().waitSemaphores(wait_info, maxWaitNanos);

    switch (result) {
    case vk::Result::eSuccess:
        break;
    default:
        std::ostringstream err;
        err << "timeline semaphore failed with " << to_string(result);
        throw std::runtime_error(err.str());
    }

    markWaitablesAsResolved(awaitables);
}

void Synchronization::hostSignal(AwaitableHandle awaitable) {
    vk::SemaphoreSignalInfo signal_info(awaitable->semaphore, awaitable->value);
    device().signalSemaphore(signal_info);
    markWaitablesAsResolved({awaitable});
}

void Synchronization::markWaitablesAsResolved(AwaitableList awaitables) {
    // observing state in semaphore X, allows us to implicitly derive observed state in semaphore Y.
    // The most intuitive way to do this would be by walking the dependency graph. But since semaphore
    // values are guaranteed to increase, that information can be tracked more efficiently in a compressed form
    // in the predecessor planing state of each waitable.
    for (const auto &awaitable : awaitables) {
        // mark the predecessors as executed
        setExecutionState(awaitable->predecessorPlaningState);
        // mark the awaitable itself as executed
        setExecutionState(awaitable->semaphoreId, awaitable->value);
    }
}

AwaitableHandle Synchronization::createAwaitable_(AwaitableHandle *predecessors, size_t predecessorsSize, vk::PipelineStageFlags stages, bool persistPredecessors) {
    // use max(preds) as supremum operation in the monotone forward analysis with zero as initial bottom value
    SemaphoreState predecessorPlaningState(m_semaphore.size(), 0);
    for (int j = 0; j < predecessorsSize; ++j) {
        predecessorPlaningState[predecessors[j]->semaphoreId] = std::max(predecessors[j]->value, predecessorPlaningState[predecessors[j]->semaphoreId]);
    }

    // one than more predecessor means that the node elimates parallelism through a join of a prior fork
    size_t NO_SEMAPHORE_PICKED_YET = std::numeric_limits<size_t>::max();
    size_t pickedSemaphoreId = NO_SEMAPHORE_PICKED_YET;

    // if (predecessors.size() == 1) {
    //  if there is only one predecessor, keep the current color/semaphore
    //  Note: this case is automatically correctly treated by the else branch.

    // pickedSemaphoreId = predecessors.data()[0]->semaphoreId;
    //} else if (predecessors.empty()) {
    // not having any predecessor is like connecting to an implicit `application start node` that has
    // an all zero planing state. This means that we can only reuse an existing semaphore if
    // the semaphore planing state is zero, a zero planing state is not possible for a semaphore
    // that was used once. Since we use semaphores immediately after creation, we thus MUST
    // create a new semaphore for nodes without predecessors by design.

    // pickedSemaphoreId = createAnotherSemaphore();

    // However, these invariants should always be relative to the observed execution state
    // so if we take: planingState - executionState = relative planing state, we could have
    // planingStates <= 0 that indicate a semaphore can be reused!

    // Note: this case is automatically correctly treated by the else branch.
    //} else {
    // writing it this way instead of iterating over `predecessorPlaningState` will first ignore colors that
    // are not yet used on the current downward path, thus forcing a more uniform color selection.
    for (int j = 0; j < predecessorsSize; ++j) {
        const auto semaphoreStateOnCurrentDownwardPath = predecessorPlaningState[predecessors[j]->semaphoreId];
        const auto semaphoreStateOnGlobalDownwardPath = m_semaphore[predecessors[j]->semaphoreId]->getPlaningState();
        // if both are are equal, we are currently the end of the downward path and can extend the path with this color
        if (semaphoreStateOnCurrentDownwardPath == semaphoreStateOnGlobalDownwardPath) {
            pickedSemaphoreId = predecessors[j]->semaphoreId;
            break;
        }
    }

    //    // none of the predecessors can extend the current downward path, lets try to find a color that is no longer in use
    //    // by eliminating ordering constraints using knowledge about the current execution state
    //    for (int j = 0; j < predecessorsSize; ++j) {
    //        const auto semaphoreStateAlreadyReached = m_executionState[predecessors[j]->semaphoreId];
    //        const auto semaphoreStateOnGlobalDownwardPath = predecessorPlaningState[predecessors[j]->semaphoreId];
    //        if (semaphoreStateOnGlobalDownwardPath <= semaphoreStateAlreadyReached) {
    //            // this means that all gpu work scheduled with the semaphore was already finished.
    //            // So the semaphore is not in use at all by pending instructions.
    //            pickedSemaphoreId = predecessors[j]->semaphoreId;
    //            break;
    //        }
    //    }

    for (int j = 0; j < m_semaphore.size(); ++j) {
        const auto globalPlaningState = m_semaphore[j]->getPlaningState();
        const auto globalExecutionState = m_executionState[j];
        if (globalPlaningState == globalExecutionState) {
            // this means that all gpu work scheduled with the semaphore was already finished.
            // So the semaphore is not in use at all by pending instructions.
            pickedSemaphoreId = j;
            break;
        }
    }

    if (pickedSemaphoreId == NO_SEMAPHORE_PICKED_YET) {
        // at this point:
        // (a) all predecessor colors were already in use
        // (b) no color was currently unused
        // => consequently, we need to allocate a new color
        pickedSemaphoreId = createAnotherSemaphore();
    }
    //}

    auto &pickedSemaphore = m_semaphore[pickedSemaphoreId];

    std::optional<std::vector<AwaitableHandle>> awaitBeforeExecution = std::nullopt;

    if (persistPredecessors) {
        // this seems to drop const without an error?
        awaitBeforeExecution = std::vector(predecessors, predecessors + predecessorsSize);
    }

    auto ret = std::make_shared<Awaitable>(Awaitable{.semaphoreId = pickedSemaphore->getId(),
                                                     .semaphore = pickedSemaphore->getHandle(),
                                                     .value = pickedSemaphore->incrementPlaningState(),
                                                     .stages = stages,
                                                     .predecessorPlaningState = predecessorPlaningState,
                                                     .awaitBeforeExecution = awaitBeforeExecution});

#ifdef VVV_SYNC_DEBUG
    write_dot_node(&ret);
    write_dot_edges(predecessors, predecessorsSize, &ret);
#endif
    return ret;
}

AwaitableHandle Synchronization::createAwaitable(AwaitableList predecessors, vk::PipelineStageFlags stages) {
    auto awaitable = createAwaitable_(predecessors.data(), predecessors.size(), stages, true);
    return awaitable;
}

size_t Synchronization::createAnotherSemaphore() {
    const auto semaphoreId = m_semaphore.size();
    if (semaphoreId == 501)
        Logger(Warn) << "vvv::Synchronization already created 500 timeline semaphores. You probably did something wrong.";

    m_semaphore.emplace_back(std::make_unique<TimelineSemaphore>(semaphoreId));
    m_executionState.emplace_back(0);
    m_semaphore[semaphoreId]->initResources(device());
    getCtx()->debugMarker->setName(m_semaphore[semaphoreId]->getHandle(), "Sync.TimelineSemaphore." + std::to_string(semaphoreId));
    return semaphoreId;
}

void Synchronization::destroySynchronizationPrimitives() {
    // TODO: await all semaphores to not crash the gpu
    m_executionState.clear();
    m_semaphore.clear();
}

void Synchronization::setExecutionState(uint32_t semaphoreId, uint64_t semaphoreValue) {
    // this is safe since the checkpointed state may online contain less semaphores than the
    // current state. (we do not free or repack/reorder semaphores.) So as long as semaphoreId was taken from
    // a waitable, this array access will be within bounds and reference the correct semaphore.
    m_executionState[semaphoreId] = std::max(semaphoreValue, m_executionState[semaphoreId]);
}

void Synchronization::setExecutionState(const SemaphoreState &executionState) {
    for (size_t semaphoreId = 0; semaphoreId < executionState.size(); semaphoreId++) {
        setExecutionState(semaphoreId, executionState[semaphoreId]);
    }
}

SemaphoreState Synchronization::checkpointPlaningState() const {
    SemaphoreState semaphoreValues;
    semaphoreValues.reserve(m_semaphore.size());

    for (const auto &semaphore : m_semaphore) {
        semaphoreValues.emplace_back(semaphore->getPlaningState());
    }

    return semaphoreValues;
}

}; // namespace vvv
