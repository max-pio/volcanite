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

#include <vvv/reflection/GraphicsPipelineReflection.hpp>

#include <vvv/core/Shader.hpp>
#include <vvv/reflection/TextureReflection.hpp>

namespace vvv {

// std::optional<vk::VertexInputBindingDescription> reflectVertexInputBindingDescriptions(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, std::string name) {
//
//     vk::VertexInputBindingDescription vertexBindingDescription = {};
//     vertexBindingDescription.binding = 0;
//     vertexBindingDescription.stride = sizeof(glm::vec3);
//     vertexBindingDescription.inputRate = vk::VertexInputRate::eVertex;
//
//
//     return vertexBindingDescription;
// }
//
// std::vector<vk::VertexInputAttributeDescription> reflectVertexAttributeDescriptions(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders) {
//     vk::VertexInputAttributeDescription vertexAttributeDescription = {};
//     vertexAttributeDescription.binding = 0;
//     vertexAttributeDescription.location = 0;
//     vertexAttributeDescription.format = vk::Format::eR32G32B32Sfloat;
//     vertexAttributeDescription.offset = 0;
//     return {};
// }

uint32_t reflectColorAttachmentLocation(vvv::GpuContextPtr ctx, std::string name, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders) {
    // first: we check all the outputs to get attachment the texture format. Required to be found!
    for (const auto &shader : shaders) {
        if (!(shader->reflectShaderStage() | vk::ShaderStageFlagBits::eFragment))
            continue;

        const auto output_ = shader->tryRawReflectOutputByName(name);
        if (!output_) {
            continue;
        }
        return output_.value()->location;
    }

    throw std::runtime_error("output name " + name + " could not be found in any of the shaders");
}

std::vector<std::pair<std::string, vk::Format>> reflectColorAttachmentInfo(vvv::GpuContextPtr ctx, const std::shared_ptr<Shader> shader) {
    assert(shader->reflectShaderStage() | vk::ShaderStageFlagBits::eFragment);

    int lastLocation = -1;

    std::vector<std::pair<std::string, vk::Format>> result = {};
    const auto outputs = shader->reflectOutputs();
    for (const auto &out : outputs) {
        assert(out->location == lastLocation + 1);
        lastLocation = static_cast<int>(out->location);
        result.emplace_back(out->name, details::spvr_refl2vk_format.at(out->format));
    }

    return result;
}

} // namespace vvv
