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

#include <vvv/reflection/UniformReflection.hpp>

namespace vvv {
std::shared_ptr<UniformReflected> reflectUniformSet(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, const std::string &name) {
    // note: uniforms are not checked for compatibility
    for (const auto &shader : shaders) {
        const auto binding_ = shader->tryRawReflectBindingByName(name);

        if (!binding_) {
            continue;
        }

        const auto binding = binding_.value();

        if (binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            throw std::runtime_error("uniform reflection, yet unsupported uniform buffer type.");
        }

        return std::make_shared<UniformReflected>(binding);
    }

    throw std::runtime_error("uniform reflection, uniform not found.");
}
} // namespace vvv
