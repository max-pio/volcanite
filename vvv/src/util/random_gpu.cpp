#include <vvv/util/random_gpu.hpp>

namespace vvv {

void uploadRandomFloatsToBuffer(std::shared_ptr<Buffer> buffer) {
    size_t elemCapacity = buffer->getByteSize() / sizeof(float);
    std::vector<float> tmp(elemCapacity);
    std::mt19937 rnd(std::time(nullptr));
    auto maxRnd = static_cast<float>(std::mt19937::max());
    for (int i = 0; i < elemCapacity; i++)
        tmp[i] = static_cast<float>(rnd()) / maxRnd;
    buffer->upload(tmp);
}


std::shared_ptr<Buffer> createRandomFloatBuffer(size_t elemCapacity, std::string label, GpuContextPtr ctx, bool uploadOnCreation) {
    auto buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = std::move(label), .byteSize = elemCapacity * sizeof(float), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if(uploadOnCreation) {
        uploadRandomFloatsToBuffer(buffer);
    }
    return buffer;
}

} // namespace vvv