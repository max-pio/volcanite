#include <vvv/core/Buffer.hpp>
#include <vvv/vk/memory.hpp>

namespace vvv {
void Buffer::createBuffer(vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryUsage, const std::string &label) {

    const auto device = m_ctx->getDevice();

    // create a staging buffer
    // TODO(Reiner): this should be cached and reused either explicitly or using a memory allocator. The probability that this has the same dimension for subsequent calls is very high for 3d+t

    // const auto textureBufferMemorySize = memorySize();
    m_buffer = device.createBuffer(vk::BufferCreateInfo(vk::BufferCreateFlags(), m_byteSize, bufferUsage));

    vk::MemoryRequirements memoryRequirements = device.getBufferMemoryRequirements(m_buffer);
    uint32_t memoryTypeIndex = getMemoryType(*m_ctx, memoryRequirements.memoryTypeBits, memoryUsage);

    m_bufferMemory = device.allocateMemory(vk::MemoryAllocateInfo(memoryRequirements.size, memoryTypeIndex));
    device.bindBufferMemory(m_buffer, m_bufferMemory, 0);

    if (!label.empty()) {
        m_ctx->debugMarker->setName(m_buffer, label);
        m_ctx->debugMarker->setName(m_bufferMemory, label);
    }

    descriptor = vk::DescriptorBufferInfo(m_buffer, 0, m_byteSize);

}

std::vector<uint8_t> Buffer::download() const {
    std::vector<uint8_t> hostMemory(m_byteSize);
    download(hostMemory);
    return hostMemory;
}

void Buffer::download(void *dest, size_t byteSize) const {
    assert(byteSize <= m_byteSize);
    const auto device = m_ctx->getDevice();
    void *deviceMemory = device.mapMemory(m_bufferMemory, 0, byteSize, vk::MemoryMapFlags());
    std::memcpy(dest, deviceMemory, byteSize);
    device.unmapMemory(m_bufferMemory);
}

void Buffer::download(void *dest, size_t deviceOffset, size_t byteSize) const {
    assert(deviceOffset + byteSize <= m_byteSize);
    const auto device = m_ctx->getDevice();
    void *deviceMemory = device.mapMemory(m_bufferMemory, deviceOffset, byteSize, vk::MemoryMapFlags());
    std::memcpy(dest, deviceMemory, byteSize);
    device.unmapMemory(m_bufferMemory);
}

void Buffer::upload(const void *rawData, size_t byteSize) const {
    const auto device = getCtx()->getDevice();
    vk::MemoryRequirements memoryRequirements = device.getBufferMemoryRequirements(getBuffer());
    assert(byteSize <= m_byteSize);
    void *data = device.mapMemory(getMemory(), 0, byteSize, vk::MemoryMapFlags());
    std::memcpy(data, rawData, byteSize);
    device.unmapMemory(getMemory());
}
void Buffer::upload(size_t device_offset, const void *rawData, size_t byteSize) const {
    const auto device = getCtx()->getDevice();
    vk::MemoryRequirements memoryRequirements = device.getBufferMemoryRequirements(getBuffer());
    assert(byteSize <= m_byteSize);
    void *data = device.mapMemory(getMemory(), device_offset, byteSize, vk::MemoryMapFlags());
    std::memcpy(data, rawData, byteSize);
    device.unmapMemory(getMemory());
}


void Buffer::uploadWithStagingBuffer(vk::CommandBuffer commandBuffer, const Buffer &staging, const void *const rawData, size_t byteSize) const {
    assert(byteSize <= m_byteSize);
    staging.upload(rawData, byteSize);

    commandBuffer.copyBuffer(staging.getBuffer(), m_buffer, vk::BufferCopy(0, 0, m_byteSize));
}

std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>> Buffer::uploadWithStagingBuffer(const void * const rawData, size_t byteSize, const detail::OpenGLStyleSubmitOptions opts) const {
    auto staging = std::make_shared<vvv::Buffer>(m_ctx, vvv::BufferSettings{
                                                            .label = "staging" + (!m_label.empty() ? "(" + m_label + ")" : ""),
                                                            .byteSize = m_byteSize,
                                                        });
    auto awaitable = m_ctx->executeCommands([&,this](vk::CommandBuffer commandBuffer) {
        uploadWithStagingBuffer(commandBuffer, *staging, rawData, byteSize);
    }, opts);
    return {awaitable, staging};
}


void Buffer::destroyBuffer() {
    const auto device = m_ctx->getDevice();

    if (m_buffer != static_cast<vk::Buffer>(nullptr)) {
        device.destroyBuffer(m_buffer);
    }
    if (m_bufferMemory != static_cast<vk::DeviceMemory>(nullptr)) {
        device.freeMemory(m_bufferMemory);
    }
}
}; // namespace vvv
