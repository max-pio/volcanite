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

#include <vvv/core/Texture.hpp>

namespace vvv {

struct TextureReflectionOptions {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    std::optional<vk::Format> format = std::nullopt;
    // support upload and download by default to ease debugging, in most cases we don't care about the potential extra performance
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
    std::set<uint32_t> queues = TextureExclusiveQueueUsage;
};

namespace details {
const std::map<SpvDim, TextureDimensions> spvr2vvv_Dimensions{
    {SpvDim1D, TextureDimensions::e1D},
    {SpvDim2D, TextureDimensions::e2D},
    {SpvDim3D, TextureDimensions::e3D},
};

const std::map<SpvReflectFormat, vk::Format> spvr_refl2vk_format{
    {SPV_REFLECT_FORMAT_UNDEFINED, vk::Format::eUndefined}, {SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT, vk::Format::eR32G32B32A32Sfloat},
    //    SPV_REFLECT_FORMAT_R32_UINT            =  98, // = VK_FORMAT_R32_UINT
    //    SPV_REFLECT_FORMAT_R32_SINT            =  99, // = VK_FORMAT_R32_SINT
    //    SPV_REFLECT_FORMAT_R32_SFLOAT          = 100, // = VK_FORMAT_R32_SFLOAT
    //    SPV_REFLECT_FORMAT_R32G32_UINT         = 101, // = VK_FORMAT_R32G32_UINT
    //    SPV_REFLECT_FORMAT_R32G32_SINT         = 102, // = VK_FORMAT_R32G32_SINT
    //    SPV_REFLECT_FORMAT_R32G32_SFLOAT       = 103, // = VK_FORMAT_R32G32_SFLOAT
    //    SPV_REFLECT_FORMAT_R32G32B32_UINT      = 104, // = VK_FORMAT_R32G32B32_UINT
    //    SPV_REFLECT_FORMAT_R32G32B32_SINT      = 105, // = VK_FORMAT_R32G32B32_SINT
    //    SPV_REFLECT_FORMAT_R32G32B32_SFLOAT    = 106, // = VK_FORMAT_R32G32B32_SFLOAT
    //    SPV_REFLECT_FORMAT_R32G32B32A32_UINT   = 107, // = VK_FORMAT_R32G32B32A32_UINT
    //    SPV_REFLECT_FORMAT_R32G32B32A32_SINT   = 108, // = VK_FORMAT_R32G32B32A32_SINT
    //    SPV_REFLECT_FORMAT_R64_UINT            = 110, // = VK_FORMAT_R64_UINT
    //    SPV_REFLECT_FORMAT_R64_SINT            = 111, // = VK_FORMAT_R64_SINT
    //    SPV_REFLECT_FORMAT_R64_SFLOAT          = 112, // = VK_FORMAT_R64_SFLOAT
    //    SPV_REFLECT_FORMAT_R64G64_UINT         = 113, // = VK_FORMAT_R64G64_UINT
    //    SPV_REFLECT_FORMAT_R64G64_SINT         = 114, // = VK_FORMAT_R64G64_SINT
    //    SPV_REFLECT_FORMAT_R64G64_SFLOAT       = 115, // = VK_FORMAT_R64G64_SFLOAT
    //    SPV_REFLECT_FORMAT_R64G64B64_UINT      = 116, // = VK_FORMAT_R64G64B64_UINT
    //    SPV_REFLECT_FORMAT_R64G64B64_SINT      = 117, // = VK_FORMAT_R64G64B64_SINT
    //    SPV_REFLECT_FORMAT_R64G64B64_SFLOAT    = 118, // = VK_FORMAT_R64G64B64_SFLOAT
    //    SPV_REFLECT_FORMAT_R64G64B64A64_UINT   = 119, // = VK_FORMAT_R64G64B64A64_UINT
    //    SPV_REFLECT_FORMAT_R64G64B64A64_SINT   = 120, // = VK_FORMAT_R64G64B64A64_SINT
    //    SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT = 121, // = VK_FORMAT_R64G64B64A64_SFLOAT
};

const std::map<SpvImageFormat, vk::Format> spvr2vk_format{
    {SpvImageFormatUnknown, vk::Format::eUndefined}, {SpvImageFormatRgba32f, vk::Format::eR32G32B32A32Sfloat}, {SpvImageFormatRgba16f, vk::Format::eR16G16B16A16Sfloat}, {SpvImageFormatR32f, vk::Format::eR32Sfloat}, {SpvImageFormatRgba8, vk::Format::eR8G8B8A8Srgb}, {SpvImageFormatRgba8Snorm, vk::Format::eR8G8B8A8Snorm},

    {SpvImageFormatRg32f, vk::Format::eR32G32Sfloat},
    {SpvImageFormatRg16f, vk::Format::eR16G16Sfloat},
    {SpvImageFormatR11fG11fB10f, vk::Format::eB10G11R11UfloatPack32},
    {SpvImageFormatR16f, vk::Format::eR16Sfloat},
    {SpvImageFormatRgba16, vk::Format::eR16G16B16A16Sfloat},
    {SpvImageFormatRgb10A2, vk::Format::eA2R10G10B10SintPack32},
    {SpvImageFormatRg16, vk::Format::eR16G16Sfloat},
    //    SpvImageFormatRg8 = 13,
    //    SpvImageFormatR16 = 14,
    //    SpvImageFormatR8 = 15,
    //    SpvImageFormatRgba16Snorm = 16,
    //    SpvImageFormatRg16Snorm = 17,
    //    SpvImageFormatRg8Snorm = 18,
    //    SpvImageFormatR16Snorm = 19,
    //    SpvImageFormatR8Snorm = 20,
    //    SpvImageFormatRgba32i = 21,
    //    SpvImageFormatRgba16i = 22,
    //    SpvImageFormatRgba8i = 23,
    //    SpvImageFormatR32i = 24,
    //    SpvImageFormatRg32i = 25,
    //    SpvImageFormatRg16i = 26,
    //    SpvImageFormatRg8i = 27,
    //    SpvImageFormatR16i = 28,
    //    SpvImageFormatR8i = 29,
    //    SpvImageFormatRgba32ui = 30,
    //    SpvImageFormatRgba16ui = 31,
    //    SpvImageFormatRgba8ui = 32,
    //    SpvImageFormatR32ui = 33,
    //    SpvImageFormatRgb10a2ui = 34,
    //    SpvImageFormatRg32ui = 35,
    //    SpvImageFormatRg16ui = 36,
    //    SpvImageFormatRg8ui = 37,
    //    SpvImageFormatR16ui = 38,
    //    SpvImageFormatR8ui = 39,
    //    SpvImageFormatR64ui = 40,
    //    SpvImageFormatR64i = 41,
};
}; // namespace details

/// Derives a texture that can be used for all the given bindings.
/// @throws std::runtime_exception if all names did not match any uniform. note that this will not throw if one variable name does not match any uniform.
/// @throws std::runtime_exception if the descriptors are incompatible.
/// @throws std::runtime_exception if the shaders have incompatible definitions for a given descriptor name.
/// @throws std::runtime_exception if all descriptors are samplers and no format is given in the arguments.
std::shared_ptr<Texture> reflectTexture(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts);

std::vector<std::shared_ptr<Texture>> reflectTextureArray(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts);

std::shared_ptr<Texture> reflectColorAttachment(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts);

}; // namespace vvv
