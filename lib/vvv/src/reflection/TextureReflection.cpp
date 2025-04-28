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

#include <vvv/reflection/TextureReflection.hpp>

// possible extension:
// there is a `accessed` flag on bindings. Could this be used to skip or ignore bindings that are not accessed?
// could generally derive a lot more things from the reflection api

namespace vvv {

std::shared_ptr<Texture> reflectTexture(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) {
    TextureDimensions dim;
    vk::ImageUsageFlags usage = opts.usage;
    auto format = opts.format;
    std::string label = "";
    uint32_t array_dims_count = 0;

    bool used = false;

    for (const auto &shader : shaders) {
        for (int j = 0; j < names.size(); ++j) {
            const auto binding_ = shader->tryRawReflectBindingByName(names.data()[j]);

            if (!binding_) {
                continue;
            }

            label = shader->label + "." + names.data()[j];

            const auto binding = binding_.value();
            used = true;

            const auto binding_dim = details::spvr2vvv_Dimensions.at(binding->image.dim);

            // a lot more can be derived here!
            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                usage |= vk::ImageUsageFlagBits::eSampled;
            } else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                usage |= vk::ImageUsageFlagBits::eStorage;
            } else {
                throw std::runtime_error("texture reflection, unable to reflect descriptor type. Maybe you can add reflection logic for it?");
            }

            array_dims_count = binding->array.dims_count;

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

    if (array_dims_count > 0) {
        Logger(Warn) << "reflecting texture array for " << label << " as single texture. Use reflectTextureArray instead of reflectTexture.";
    }

    if (!used) {
        std::string namesStr;
        for (int i = 0; i < names.size(); i++)
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

std::vector<std::shared_ptr<Texture>> reflectTextureArray(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) {
    TextureDimensions dim;
    vk::ImageUsageFlags usage = opts.usage;
    auto format = opts.format;
    std::string label = "";
    uint32_t array_dims_count = 0;
    uint32_t array_dims[32];

    bool used = false;

    for (const auto &shader : shaders) {
        for (int j = 0; j < names.size(); ++j) {
            const auto binding_ = shader->tryRawReflectBindingByName(names.data()[j]);

            if (!binding_) {
                continue;
            }

            label = shader->label + "." + names.data()[j];

            const auto binding = binding_.value();
            used = true;

            const auto binding_dim = details::spvr2vvv_Dimensions.at(binding->image.dim);

            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                usage |= vk::ImageUsageFlagBits::eSampled;
            } else if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                usage |= vk::ImageUsageFlagBits::eStorage;
            } else {
                throw std::runtime_error("texture reflection, unable to reflect descriptor type. Maybe you can add reflection logic for it?");
            }

            array_dims_count = binding->array.dims_count;
            for (int d = 0; d < array_dims_count; d++)
                array_dims[d] = binding->array.dims[d];

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

    if (array_dims_count == 0) {
        Logger(Warn) << "reflecting single texture " << label << " as array texture. Use reflectTexture instead of reflectTextureArray.";
    }

    if (!used) {
        std::string namesStr;
        for (int i = 0; i < names.size(); i++)
            namesStr += ((i != 0) ? "|" : "") + names.data()[i];
        throw std::runtime_error("none of the given uniform names '" + namesStr + "' could be found in any of the shaders");
    }

    if (!format) {
        throw std::runtime_error("texture reflection, unable to derive image format, specify one explicitly");
    }

    // create a flattened 1D array containing all textures.
    // could store the texture array index in a more useful way instead of just in the string label.
    uint32_t number_of_textures = 1;
    for (int i = 0; i < array_dims_count; i++)
        number_of_textures *= array_dims[i];
    std::vector<std::shared_ptr<Texture>> textures(number_of_textures);
    uint32_t idx[32];
    for (int t = 0; t < number_of_textures; t++) {

        std::stringstream array_label;
        array_label << label;
        uint32_t scale = 1u;
        for (int d = 0; d < array_dims_count; d++) {
            idx[d] = (t / scale) % array_dims[d];
            scale *= array_dims[d];
            array_label << "[" << idx[d] << "]";
        }

        auto texture = std::make_shared<Texture>(ctx, format.value(), dim, opts.width, opts.height, opts.depth,
                                                 usage, opts.queues);
        texture->setName(array_label.str());
        textures[t] = texture;
    }
    return textures;
}

std::shared_ptr<Texture> reflectColorAttachment(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) {
    vk::ImageUsageFlags usage = opts.usage | vk::ImageUsageFlagBits::eColorAttachment;
    auto format = opts.format;

    bool used = false;

    // first: we check all the outputs to get attachment the texture format. Required to be found!
    for (const auto &shader : shaders) {
        if (!(shader->reflectShaderStage() | vk::ShaderStageFlagBits::eFragment))
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

} // namespace vvv