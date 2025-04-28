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

#include "preamble_forward_decls.hpp"

#include <memory>

namespace vvv {
class WithGpuContext {
  protected:
    WithGpuContext(GpuContextPtr ctx) : m_ctx(ctx) {}

  public:
    [[nodiscard]] GpuContextPtr getCtx() const;
    vk::Device device() const;
    std::shared_ptr<DebugUtilities> debug() const;

  protected:
    void setCtx(GpuContextPtr ctx) { m_ctx = ctx; }

  private:
    GpuContext const *m_ctx;
};
}; // namespace vvv
