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
#include "preamble.hpp"

namespace vvv {

struct BufferSettings {
    std::string label;
    size_t byteSize;
    vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
    vk::MemoryPropertyFlags memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
};

struct Buffer {

    Buffer(GpuContextPtr ctx, const BufferSettings &settings) : m_ctx(ctx), m_byteSize(settings.byteSize), m_label(settings.label) { createBuffer(settings.usage, settings.memoryUsage, settings.label); }

    vk::DeviceAddress getDeviceAddress() const;
    /// splits the 64 bit buffer device address into two 32 bit uint components. For usage with GL_EXT_buffer_reference_uvec2
    static void deviceAddressUvec2(vk::DeviceAddress address, uint32_t xy[2]) {
        xy[0] = static_cast<uint32_t>(address);
        xy[1] = static_cast<uint32_t>(address >> 32);
    }

    [[nodiscard]] std::vector<uint8_t> download() const;
    void download(void *dest, size_t byteSize) const;
    void download(void *dest, size_t deviceOffset, size_t byteSize) const;
    template <typename T>
    void download(std::vector<T> &dest) const { download(dest.data(), dest.size() * sizeof(T)); };

    /// upload data directly (map, memcpy, unmap). Requires eHostVisible
    void upload(const void *rawData, size_t byteSize) const;
    void upload(size_t device_offset, const void *rawData, size_t byteSize) const;
    /// upload data directly (map, memcpy, unmap). Requires eHostVisible
    template <typename T>
    void upload(const std::vector<T> &data) const {
        upload(reinterpret_cast<const void *>(data.data()), data.size() * sizeof(T));
    }

    /// upload() data to staging buffer and copy to this buffer
    void uploadWithStagingBuffer(vk::CommandBuffer commandBuffer, const Buffer &staging, const void *rawData, size_t byteSize, size_t dstOffset = 0ul) const;
    /// upload() data to new staging buffer and copy to this buffer using Awaitable API
    std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>> uploadWithStagingBuffer(const void *const rawData, size_t byteSize, const detail::OpenGLStyleSubmitOptions &opts = {}) const;
    std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>> uploadWithStagingBuffer(const void *const rawData, size_t byteSize, size_t dstOffset, const detail::OpenGLStyleSubmitOptions &opts = {}) const;
    /// upload() data to new staging buffer and copy to this buffer using Awaitable API
    template <typename T>
    std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>> uploadWithStagingBuffer(const std::vector<T> &data, const detail::OpenGLStyleSubmitOptions opts = {}) const {
        return uploadWithStagingBuffer(data.data(), data.size() * sizeof(T), opts);
    }

    [[nodiscard]] vk::Buffer getBuffer() const { return m_buffer; }
    [[nodiscard]] vk::DeviceMemory getMemory() const { return m_bufferMemory; }
    [[nodiscard]] size_t getByteSize() const { return m_byteSize; }
    [[nodiscard]] GpuContextPtr getCtx() const { return m_ctx; }

    ~Buffer() { destroyBuffer(); }

    template <typename T>
    struct uniform;

  public:
    vk::DescriptorBufferInfo descriptor = {};

  private:
    GpuContextPtr m_ctx;
    size_t m_byteSize;
    std::string m_label;

    vk::Buffer m_buffer = nullptr;
    vk::DeviceMemory m_bufferMemory = nullptr;

    void destroyBuffer();
    void createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryUsage, const std::string &label);
};

template <typename T>
struct Buffer::uniform : public Buffer {
    /// Create a rgba8u texture that can be used for writing in a compute shader and blitting to the graphics queue
    explicit uniform(GpuContextPtr ctx, const std::string label = "")
        : Buffer(ctx, BufferSettings{.label = label,
                                     .byteSize = sizeof(T),
                                     .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                                     .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}) {}
};

}; // namespace vvv
