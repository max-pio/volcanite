#pragma once

#include "preamble_forward_decls.hpp"

#include <memory>

namespace vvv {
class WithGpuContext {
protected:
    WithGpuContext(GpuContextPtr ctx) : m_ctx(ctx) {}

public:
    GpuContextPtr getCtx() const;
    vk::Device device() const;
    std::shared_ptr<DebugUtilities> debug() const;

protected:
    void setCtx(GpuContextPtr ctx) { m_ctx = ctx; }

private:
    GpuContext const *m_ctx;
};
}; // namespace vvv
