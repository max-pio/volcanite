#include <vvv/core/GpuContext.hpp>
#include <vvv/core/WithGpuContext.hpp>

namespace vvv {
GpuContextPtr WithGpuContext::getCtx() const { return m_ctx; }
vk::Device WithGpuContext::device() const { return m_ctx->getDevice(); }
std::shared_ptr<DebugUtilities> WithGpuContext::debug() const { return m_ctx->debugMarker; }
}; // namespace vvv
