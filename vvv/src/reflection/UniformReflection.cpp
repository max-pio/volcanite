#include <vvv/reflection/UniformReflection.hpp>

namespace vvv {
    std::shared_ptr<UniformReflected> reflectUniformSet(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, const std::string& name) {
        // TODO(Reiner): check uniforms for compatibility
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
}
