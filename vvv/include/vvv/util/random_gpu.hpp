#pragma once

#include <random>
#include <memory>

#include <vvv/core/preamble_forward_decls.hpp>
#include <vvv/core/Buffer.hpp>

namespace vvv {

void uploadRandomFloatsToBuffer(std::shared_ptr<Buffer> buffer);
std::shared_ptr<Buffer> createRandomFloatBuffer(size_t elemCapacity, std::string label, GpuContextPtr ctx, bool uploadOnCreation=true);

} // namespace vvv