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

#include <vvv/core/preamble_forward_decls.hpp>

#include <vvv/core/Buffer.hpp>
#include <vvv/core/MultiBuffering.hpp>
#include <vvv/core/Shader.hpp>

#include <SPIRV-Reflect/spirv_reflect.h>

#include <typeinfo>

namespace vvv {

// reflecting vertex shader inputs is not simple because bindings and alignments can have arbitrary structures
// std::vector<vk::VertexInputBindingDescription> reflectVertexInputBindingDescriptions(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders);
// std::vector<vk::VertexInputAttributeDescription> reflectVertexAttributeDescriptions(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders);

uint32_t reflectColorAttachmentLocation(vvv::GpuContextPtr ctx, std::string name, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders);

/// @return name and format of all color output attachments as an ordered vector.
std::vector<std::pair<std::string, vk::Format>> reflectColorAttachmentInfo(vvv::GpuContextPtr ctx, std::shared_ptr<Shader> shader);

} // namespace vvv
