#include <vvv/reflection/TextureReflection.hpp>

namespace vvv {

std::shared_ptr<Texture> reflectTexture(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) {
    TextureDimensions dim;
    vk::ImageUsageFlags usage = opts.usage;
    auto format = opts.format;
    std::string label = "";

    bool used = false;

    for (const auto &shader : shaders) {
        // TODO(Reiner): there is a `accessed` flag on bindings. not sure how it works... but we could probably
        // skip or ignore bindings that are not accessed...
        for (int j = 0; j < names.size(); ++j) {
            const auto binding_ = shader->tryRawReflectBindingByName(names.data()[j]);

            if (!binding_) {
                continue;
            }

            label = shader->label + "." + names.data()[j];

            const auto binding = binding_.value();

            used = true;

            const auto binding_dim = details::spvr2vvv_Dimensions.at(binding->image.dim);

            // TODO(Reiner): we can derive a lot more things here :)
            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                usage |= vk::ImageUsageFlagBits::eSampled;
            } else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                usage |= vk::ImageUsageFlagBits::eStorage;
            } else {
                throw std::runtime_error("texture reflection, unable to reflect descriptor type. Maybe you can add reflection logic for it?");
            }

            assert(binding->array.dims_count == 0 && "texture reflection, arrays currently unsupported. Maybe you can implement it?");
            assert(binding->count == 1 && "texture reflection, arrays currently unsupported. Maybe you can implement it?");

            if (!format && binding->image.image_format != SpvImageFormatUnknown) {
                format = details::spvr2vk_format.at(binding->image.image_format);
            }

            if (j == 0) {
                dim = binding_dim;
            } else {
                if (binding_dim != dim) {
                    throw std::runtime_error("texture reflection, incompatible binding dimensions");
                }
                if (format && binding->image.image_format != SpvImageFormatUnknown && format != static_cast<vk::Format>(binding->image.image_format)) {
                    throw std::runtime_error("texture reflection, incompatible image formats");
                }
            }
        }
    }

    if (!used) {
        std::string namesStr;
        for(int i = 0; i < names.size(); i++)
            namesStr += ((i != 0) ? "|" : "") + names.data()[i];
        throw std::runtime_error("none of the given uniform names '" + namesStr + "' could be found in any of the shaders");
    }

    if (!format) {
        throw std::runtime_error("texture reflection, unable to derive image format, specify one explicitly");
    }

    auto texture = std::make_shared<Texture>(ctx, format.value(), dim, opts.width, opts.height, opts.depth, usage, opts.queues);
    texture->setName(label);
    return texture;
}

// TODO(Max) pull this upwards inside one unified method where colorAttachments and textures can be reflected in one command
std::shared_ptr<Texture> reflectColorAttachment(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) {
    vk::ImageUsageFlags usage = opts.usage | vk::ImageUsageFlagBits::eColorAttachment;
    auto format = opts.format;

    bool used = false;

    // first: we check all the outputs to get attachment the texture format. Required to be found!
    for (const auto &shader : shaders) {
        if(!(shader->reflectShaderStage() | vk::ShaderStageFlagBits::eFragment))
            continue;

        for (int j = 0; j < names.size(); ++j) {
            const auto output_ = shader->tryRawReflectOutputByName(names.data()[j]);

            if (!output_) {
                continue;
            }

            const auto output = output_.value();

            used = true;

            assert(output->array.dims_count == 0 && "color attachment reflection, arrays currently unsupported. Maybe you can implement it?");

            if (!format && output->format != SPV_REFLECT_FORMAT_UNDEFINED) {
                format = details::spvr_refl2vk_format.at(output->format);
            }

            if (j > 0) {
                if (format && output->format != SPV_REFLECT_FORMAT_UNDEFINED && format != static_cast<vk::Format>(output->format)) {
                    throw std::runtime_error("texture reflection, incompatible image formats");
                }
            }
        }
    }
    if (!used) {
        throw std::runtime_error("none of the given output names could be found in any of the shaders");
    }

    // second: we search for all descriptors that we can find
    for (const auto &shader : shaders) {
        for (int j = 0; j < names.size(); ++j) {
            const auto binding_ = shader->tryRawReflectBindingByName(names.data()[j]);

            if (!binding_) {
                continue;
            }

            const auto binding = binding_.value();
            if (details::spvr2vvv_Dimensions.at(binding->image.dim) != TextureDimensions::e2D) {
                throw std::runtime_error("output attachment reflection, incompatible binding dimensions has to be 2D");
            }

            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                usage |= vk::ImageUsageFlagBits::eSampled;
            } else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                usage |= vk::ImageUsageFlagBits::eStorage;
            } else {
                throw std::runtime_error("texture reflection, unable to reflect descriptor type. Maybe you can add reflection logic for it?");
            }

            assert(binding->array.dims_count == 0 && "texture reflection for color attachment, arrays currently unsupported. Maybe you can implement it?");
            assert(binding->count == 1 && "texture reflection for color attachment, arrays currently unsupported. Maybe you can implement it?");

            if (!format && binding->image.image_format != SpvImageFormatUnknown) {
                format = details::spvr2vk_format.at(binding->image.image_format);
            }

            if (format && binding->image.image_format != SpvImageFormatUnknown && format != static_cast<vk::Format>(binding->image.image_format)) {
                throw std::runtime_error("texture reflection for color attachment, incompatible image formats");
            }
        }
    }

    if (!format) {
        throw std::runtime_error("color attachment reflection, unable to derive image format, specify one explicitly");
    }

    return std::make_shared<Texture>(ctx, format.value(), TextureDimensions::e2D, opts.width, opts.height, 1, usage, opts.queues);
}

}