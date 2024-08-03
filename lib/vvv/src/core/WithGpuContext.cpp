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

#include <vvv/core/GpuContext.hpp>
#include <vvv/core/WithGpuContext.hpp>

namespace vvv {
GpuContextPtr WithGpuContext::getCtx() const { return m_ctx; }
vk::Device WithGpuContext::device() const { return m_ctx->getDevice(); }
std::shared_ptr<DebugUtilities> WithGpuContext::debug() const { return m_ctx->debugMarker; }
}; // namespace vvv
