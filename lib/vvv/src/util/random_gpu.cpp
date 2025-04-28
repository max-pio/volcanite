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

#include <vvv/util/random_gpu.hpp>

namespace vvv {

void uploadRandomFloatsToBuffer(const std::shared_ptr<Buffer> &buffer) {
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
    if (uploadOnCreation) {
        uploadRandomFloatsToBuffer(buffer);
    }
    return buffer;
}

} // namespace vvv