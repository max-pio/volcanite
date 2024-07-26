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

#include <bit>
#include <functional>
#include "vvv/core/GpuContext.hpp"
#include "vvv/util/Logger.hpp"
#include "vvv/vk/destroy.hpp"
#include "vvv/vk/memory.hpp"

// Some things that could be changed here:
//    * if descriptorsets should also be created here: doesn't make sense.. we would have to know about the DescriptorSetLayout which may include other buffers or texture samplers as well.
//    * right now, memory is only allocated at initialization time. This may lead to unnecessary high memory consumption and is not very flexible if we want to change the buffer size later.
//    * as you may notice, this is header only. This is because of the template and increases compile times real bad. We may put the definitions in a cpp file and declare all needed templates here.
//    * Currently, only single value buffers (for uniform variables/structs) can be managed with a hash. Hashing large arrays of memory would be expensive, but we could allow the array buffer to work
//      like this too.

namespace vvv {

/// Hash an arbitrary memory block of size byte_size starting at data.
/// @param combine_hash can be initialized with a hash to combine hashes
static size_t hashMemory(const void *data, size_t byte_size, size_t combine_hash = 0) {
    size_t hash = combine_hash;
    auto p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < byte_size; i++)
        hash = (std::hash<unsigned char>{}(p[i]) ^ (std::rotl<size_t>(hash, 1)));
    return hash;
}

/// Manages a buffer for each in flight frame containing max_elements elements of type T. In case of a single uniform this would be 1 element of type Uniform_[...]. In case of a vertex buffer this
/// would be max_vertex_count elements of type glm::vec3. The data and count of elements can be changed using setData(...), but can never be higher than max_elements since it is assumed that device
/// memory is only allocated and assigned at initialization time.
/// \nn
/// This class is purely virtual:\n
///   - for single Uniform variables or structs (i.e. small buffers), use the MultiFrameValueBuffer that manages data changes with a hash.\n
///   - for arrays of element type T (i.e. large buffers), use the MultiFrameArrayBuffer that manages data changes with a dirty flag.\n
template <typename T> class MultiFrameBuffer {
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3; // TODO(Reiner): read multibuffering info!
public:
    static constexpr int AUTO_FRAME_IN_FLIGHT = -1;

    MultiFrameBuffer() = default;
    ~MultiFrameBuffer() {
        if(m_initialized)
        {
            Logger(WARN) << "MultiFrameBuffer " << m_label << " not released before destruction!";
            releaseResources();
        }
    }


    /// Creates a buffer with assigned device memory for a maximum number of max_elements elements.
    virtual void initializeResources(GpuContext *ctx, size_t max_elements, vk::BufferUsageFlags bufferUsageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, const std::string& label) {
        m_ctx = ctx;
        m_label = label;

        if(max_elements > 1 && sizeof(T) % 16 != 0)
        {
            Logger(WARN) << "Array elements for MultiFrameBuffer " << label << " are not 16 byte aligned (" << sizeof(T) << " bytes). Use std430 in shader.";
        }

        m_reservedByteSize = sizeof(T) * max_elements;
        m_buffers.resize(MAX_FRAMES_IN_FLIGHT, nullptr);
        m_memories.resize(MAX_FRAMES_IN_FLIGHT, nullptr);

        for (auto i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            const auto [buffer, mem] = createBuffer(*ctx, m_reservedByteSize, bufferUsageFlags, memoryPropertyFlags);
            m_buffers[i] = buffer;
            m_memories[i] = mem;
            ctx->debugMarker->setName(buffer, label + ".m_buffers." + std::to_string(i));
            ctx->debugMarker->setName(mem, label + ".m_memories." + std::to_string(i));
        }
        m_initialized = true;
    }

    //    It's not that simple: A DescriptorSet containing bindings for multiple buffers/textures/etc can't be created here since the layout has to be given from outside.
    //    A top-level Manager-class that can create its DescrSets would be required? something like BufferCollection({{BufferType::Texture, size, ...}, {BufferType::UniformBuffer, max_elems, ...}, ...}) void createDescriptorSets(vk::DescriptorPool pool, vk::DescriptorSetLayout>& layout, vk::DescriptorType type, uint32_t binding) {
    //        const std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(MAX_FRAMES_IN_FLIGHT, layout);
    //        vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(pool, layouts);
    //        assert(m_ctx.descriptorSets.size() == MAX_FRAMES_IN_FLIGHT);
    //        for (int i = 0; i < m_gpu.descriptorSets.size(); ++i) {
    //            ctx->debugMarker->setName(m_gpu.descriptorSets[i], "NastjaParams.m_gpu.descriptorSets." + std::to_string(i));
    //            // write descriptor set
    //            const std::vector<vk::DescriptorBufferInfo> uniformDataBufferInfo{{m_gpu.uniform.getBuffer(i), 0, sizeof(Uniform_Nastja_Data)},};
    //            const auto descSet = m_gpu.descriptorSets[i];
    //            std::vector<vk::WriteDescriptorSet> renderWriteDescriptorSets = {vk::WriteDescriptorSet(descSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, uniformDataBufferInfo),};
    //            ctx->getDevice().updateDescriptorSets(renderWriteDescriptorSets, {});
    //        }
    //    }
    //
    //    /// Calls updateDeviceMemory if the frame is flagged dirty and returns the descriptorset for this buffer.
    //    vk::DescriptorSet getDescriptorSet(uint32_t frameinFlightID) {
    //        assert(m_hasDescriptorSets);
    //        updateDeviceMemory(frameinFlightID);
    //        return m_descriptorSets[frameinFlightID];
    //    }

    void releaseResources() {
        if (!m_initialized)
            return;

        auto device = m_ctx->getDevice();

        //        if(m_hasDescriptorSets)
        //        {
        //            VK_DEVICE_DESTROY_ALL(device, m_descriptorSets)
        //            m_hasDescriptorSets = false;
        //        }

        VK_DEVICE_DESTROY_ALL(device, m_buffers)
        VK_DEVICE_FREE_ALL_MEMORY(device, m_memories)
        m_reservedByteSize = 0;
        m_ctx = nullptr;
        m_initialized = false;
    }

    bool isInitialized() const { return m_initialized; }

    /// Returns a reference to this buffer. Because of the possible memory updates, it is assumed that all old usages of the buffer are finished when getBuffer is called again.
    /// @param frameInFlightID if assigned, the frameID of the returned GPU buffer. Otherwise, the current frameInFlightID of the WSI is used.
    /// @param updateOnDemand if true, updateDeviceMemory is called to update the GPU memory on demand
    vk::Buffer *getBuffer(uint32_t frameInFlightID = AUTO_FRAME_IN_FLIGHT, bool updateOnDemand = false) {
        assert(m_initialized);
        if(frameInFlightID == AUTO_FRAME_IN_FLIGHT)
            frameInFlightID = m_ctx->getWsi()->currentInFlightFrameIndex();
        assert(frameInFlightID < MAX_FRAMES_IN_FLIGHT);

        if(updateOnDemand)
            updateDeviceMemoryOnDemand(frameInFlightID);
        return &m_buffers[frameInFlightID];
    }

    /// Updates the device memory for the given frame if the data changed since the last update.
    virtual void updateDeviceMemoryOnDemand(uint32_t frameInFlightID) = 0;

    /// Updates the device memory regardless of if the data changed or not.
    /// @param frameInFlightID if assigned, the frameID of the returned GPU buffer. Otherwise, the current frameInFlightID of the WSI is used.
    virtual void updateDeviceMemoryForced(uint32_t frameInFlightID = AUTO_FRAME_IN_FLIGHT) {
        if (!m_initialized || m_element_count_to_upload == 0)
            return;

        if(frameInFlightID == AUTO_FRAME_IN_FLIGHT)
            frameInFlightID = m_ctx->getWsi()->currentInFlightFrameIndex();

        vk::DeviceSize size = static_cast<vk::DeviceSize>(m_element_count_to_upload) * sizeof(T);
        assert(size <= m_reservedByteSize); // automated resizing not implemented
        assert(m_data_to_upload);

        void *bufferData;
        bufferData = m_ctx->getDevice().mapMemory(m_memories[frameInFlightID], 0, size);
        memcpy(bufferData, m_data_to_upload, size);
        m_ctx->getDevice().unmapMemory(m_memories[frameInFlightID]);
    }


    void clearData() { m_element_count_to_upload = 0; }

    /// Returns the number of elements that should be uploaded, i.e. the last element count passed to setData. The buffer may not yet contain this number of elements until getBuffer was called.
    size_t getElementCount() const { return m_element_count_to_upload; }

    /// Returns the reserved byte size for the buffer on the device. The used size may be smaller than this but must never be higher.
    vk::DeviceSize getBufferByteSize() const { return m_reservedByteSize; }

protected:
    template <typename U> using ForEachInFlightFrame = std::vector<U>; // TODO(Reiner): @deprecated, use MultiBuffering on Wsi instead

    GpuContext *m_ctx = nullptr;
    std::string m_label;
    const T *m_data_to_upload = nullptr;
    size_t m_element_count_to_upload = 0;
    bool m_initialized = false;
    ForEachInFlightFrame<vk::Buffer> m_buffers = {};
    ForEachInFlightFrame<vk::DeviceMemory> m_memories = {};
    vk::DeviceSize m_reservedByteSize = 0;
    //    // descriptor sets
    //    bool m_hasDescriptorSets = false;
    //    ForEachInFlightFrame<vk::DescriptorSet> m_descriptorSets = {};
};


/// Use this managed multi frame buffer for Uniform variables and structs or more precisely: single elements of type T.
/// Internally, a hash of the uploaded object is used to track if the device memory has to be updated.
template <typename T> class MultiFrameValueBuffer : public MultiFrameBuffer<T> {
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3; // TODO(Reiner): read multibuffering info!
public:
    void initializeResources(GpuContext *ctx, size_t max_elements, vk::BufferUsageFlags bufferUsageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, const std::string& label) override {
        MultiFrameBuffer<T>::initializeResources(ctx, max_elements, bufferUsageFlags, memoryPropertyFlags, label);

        m_lastHash.resize(MAX_FRAMES_IN_FLIGHT, 0);
        MultiFrameBuffer<T>::m_data_to_upload = &m_value;
    }

    /// All following calls to getBuffer will update the buffer memory with ONE element containing the content data. Useful for uniform buffers.
    void setDataByValue(const T data) {
        m_value = data;
        MultiFrameBuffer<T>::m_element_count_to_upload = 1;
    }

    /// Computes a hash of the data to upload and updates the device memory if this hash differs from the currently uploaded data.
    /// @param frameInFlightID if assigned, the frameID of the returned GPU buffer. Otherwise, the current frameInFlightID of the WSI is used.
    void updateDeviceMemoryOnDemand(uint32_t frameInFlightID = MultiFrameBuffer<T>::AUTO_FRAME_IN_FLIGHT) {
        if(frameInFlightID == MultiFrameBuffer<T>::AUTO_FRAME_IN_FLIGHT)
            frameInFlightID = MultiFrameBuffer<T>::m_ctx->getWsi()->currentInFlightFrameIndex();

        size_t hash = hashMemory(MultiFrameBuffer<T>::m_data_to_upload, sizeof(T));
        if (hash != m_lastHash[frameInFlightID]) {
            m_lastHash[frameInFlightID] = hash;
            MultiFrameBuffer<T>::updateDeviceMemoryForced(frameInFlightID);
        }
    }

private:
    template <typename U> using ForEachInFlightFrame = std::vector<U>; // TODO(Reiner): @deprecated, use MultiBuffering on Wsi instead
    ForEachInFlightFrame<size_t> m_lastHash = {};
    T m_value = {};
};


/// Use this managed multi frame buffer for arrays containing many elements of type T.
/// Internally, a dirty flag is used to track if the device memory has to be updated.
template <typename T> class MultiFrameArrayBuffer : public MultiFrameBuffer<T> {
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3; // TODO(Reiner): read multibuffering info!
public:
    void initializeResources(GpuContext *ctx, size_t max_elements, vk::BufferUsageFlags bufferUsageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, const std::string& label) override {
        MultiFrameBuffer<T>::initializeResources(ctx, max_elements, bufferUsageFlags, memoryPropertyFlags, label);

        m_dirty.resize(MAX_FRAMES_IN_FLIGHT, true);
    }

    /// All following calls to getBuffer will update the buffer memory with count elements from location data. data must point to a valid location until the next call to setData.
    void setData(const T *data, size_t count) {
        MultiFrameBuffer<T>::m_data_to_upload = data;
        MultiFrameBuffer<T>::m_element_count_to_upload = count;
        setDirty();
    }

    /// All following calls to getBuffer will update the buffer memory from the vector data. data must exist until the next call to setData.
    void setData(std::vector<T> *data) { setData(data->data(), data->size()); }

    /// Triggers a forced memory update on the next call to getBuffer(...).
    void setDirty() { std::fill(m_dirty.begin(), m_dirty.end(), true); }

    /// Updates the device memory if the dirty flag was set for this frame in flight, e.g. after calls to setData(...) or setDirty().
    /// @param frameInFlightID if assigned, the frameID of the returned GPU buffer. Otherwise, the current frameInFlightID of the WSI is used.
    void updateDeviceMemoryOnDemand(uint32_t frameInFlightID = MultiFrameBuffer<T>::AUTO_FRAME_IN_FLIGHT) {
        if(frameInFlightID == MultiFrameBuffer<T>::AUTO_FRAME_IN_FLIGHT)
            frameInFlightID = MultiFrameBuffer<T>::m_ctx->getWsi()->currentInFlightFrameIndex();

        if (!MultiFrameBuffer<T>::m_initialized || !m_dirty[frameInFlightID] || MultiFrameBuffer<T>::m_element_count_to_upload == 0)
            return;
        updateDeviceMemoryForced(frameInFlightID);
    }

    void updateDeviceMemoryForced(uint32_t frameInFlightID) override {
        MultiFrameBuffer<T>::updateDeviceMemoryForced(frameInFlightID);
        m_dirty[frameInFlightID] = false;
    }

private:
    template <typename U> using ForEachInFlightFrame = std::vector<U>; // TODO(Reiner): @deprecated, use MultiBuffering on Wsi instead
    ForEachInFlightFrame<bool> m_dirty = {};
};

} // namespace vvv
