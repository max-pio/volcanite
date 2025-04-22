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

#include <vvv/core/preamble_forward_decls.hpp>

namespace vvv {

///  @brief A timeline semaphore is a `counting semaphore`.
///
/// - wait until semaphore value is >= N\n
/// - the execution state is the semaphore value M_h known to the CPU (host)\n
/// - the actual execution state is the semaphore value M_d on the GPU (device)\n
/// - M_d >= M_h\n
/// - the planing state is the maximal semahpore value M_p of all waited (signaled and unsignaled) semaphore values.\n
///   Consequently M_d = M_p implies that all planned work has already executed.\n
/// - M_p >= M_d >= M_h\n
class TimelineSemaphore {
  public:
    /// @param semaphoreId some arbitrary integer that can be used by external code to map this semaphore to metadata
    explicit TimelineSemaphore(size_t semaphoreId = 0) : m_semaphoreId(semaphoreId) {}
    ~TimelineSemaphore() { deallocateResources(); }

    vk::Semaphore getHandle() const { return m_semaphore; }

    void initResources(vk::Device device) {
        if (m_device != static_cast<vk::Device>(nullptr)) {
            return;
        }

        m_device = device;
        m_semaphore = createTimelineSemaphore();
    }

    void deallocateResources() { VK_DEVICE_DESTROY(m_device, m_semaphore); }

    /// Increment the planing state. This effectively reserves the returned semaphore value for the caller.
    /// The caller should use that value in the list of semaphores to signal in some Vulkan API call.
    uint64_t incrementPlaningState() { return m_nextId++; }

    /// get the highest semaphore value __already in use__.
    uint64_t getPlaningState() const { return m_nextId - 1; }
    size_t getId() const { return m_semaphoreId; }

  private:
    vk::Semaphore createTimelineSemaphore() {
        vk::SemaphoreCreateInfo create_info;
        vk::SemaphoreTypeCreateInfoKHR type_create_info(vk::SemaphoreType::eTimeline, 0); // generateSubmitId()
        create_info.pNext = &type_create_info;
        return m_device.createSemaphore(create_info);
    }

    size_t m_semaphoreId;

    /// the timeline semaphore `m_semaphore` was used to plan a schedule up to `m_nextId`
    uint64_t m_nextId = 1; // zero is the initial state
    vk::Semaphore m_semaphore = nullptr;
    vk::Device m_device = nullptr;
};

}; // namespace vvv