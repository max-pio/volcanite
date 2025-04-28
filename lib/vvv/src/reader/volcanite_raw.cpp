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

#include <utility>
#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>
#include <vvv/util/Logger.hpp>

#include <cmath>
#include <iostream>

namespace vvv {

template <typename T>
std::shared_ptr<Volume<T>> load_volcanite_raw_(std::string url, std::string formatLabel, size_t bitwidth, vk::Format gpuFormat) {
    std::ifstream vraw(url, std::ios_base::in | std::ios_base::binary);
    if (!vraw.is_open()) {
        std::ostringstream err;
        err << "unable to open Volcanite RAW file at: " << url << "\n";
        Logger(Error) << err.str();
        throw std::runtime_error(err.str());
    }

    // read dimension
    uint64_t img_width = 0;
    uint64_t img_height = 0;
    uint64_t img_depth = 0;
    uint16_t bits_per_sample = 0;

    // read header
    std::string line;
    // first line contains space seperated width height depth
    if (!std::getline(vraw, line)) {
        vraw.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    std::istringstream sizes(line);
    sizes >> img_width >> img_height >> img_depth;
    // second line contains data type
    if (!std::getline(vraw, line)) {
        vraw.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (line != formatLabel)
        throw std::runtime_error("data type " + line + " does not equal to requested format " + formatLabel);
    if (line == "uint32")
        bits_per_sample = 32;
    else if (line == "uint16")
        bits_per_sample = 16;
    else if (line == "uint8")
        bits_per_sample = 8;
    else {
        vraw.close();
        throw std::runtime_error("unexpected data type " + line + " in " + url);
    }

    // header is always two lines
    float max_dim = static_cast<float>(std::max(img_width, std::max(img_height, img_depth)));
    float physical_size_x = static_cast<float>(img_width) / max_dim;
    float physical_size_y = static_cast<float>(img_height) / max_dim;
    float physical_size_z = static_cast<float>(img_depth) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y) || !std::isfinite(physical_size_z)) {
        vraw.close();
        throw std::invalid_argument("invalid Volcanite RAW physical volume size");
    }

    // 2048^3 voxels. thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples, 32GiB for 32bit samples.
    const uint64_t MAX_ALLOWED_VOXELS = 8589934592ul;
    const uint64_t voxel_count = img_width * img_height * img_depth;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        vraw.close();
        std::string err = "Volcanite RAW volume exceeds maximum allowed size with ";
        err += str(glm::uvec3(img_width, img_height, img_depth)) + " >= " + std::to_string(MAX_ALLOWED_VOXELS);
        throw std::invalid_argument(err);
    }

    size_t byte_size = voxel_count * (bits_per_sample / 8);
    std::vector<T> payload(voxel_count);

    // read binary data inline
    vraw.read(reinterpret_cast<char *>(payload.data()), byte_size);

    if (!vraw) {
        vraw.close();
        throw std::runtime_error("only " + std::to_string(vraw.gcount()) + " bytes of expected " + std::to_string(byte_size) + " bytes could be read from Volcanite RAW file.");
    }

    vraw.close();
    return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
}

template <typename T>
std::shared_ptr<Volume<T>> load_volcanite_raw_with_cast_(std::string url, std::string formatLabel, vk::Format gpuFormat) {
    throw std::runtime_error("load_volcanite_raw_with_cast not implemented yet!");
}

template <>
std::shared_ptr<Volume<uint64_t>> Volume<uint64_t>::load_volcanite_raw(std::string path, bool allowCast) {
    return allowCast ? load_volcanite_raw_with_cast_<uint64_t>(path, "uint64", vk::Format::eR64Uint) : load_volcanite_raw_<uint64_t>(path, "uint64", 64, vk::Format::eR64Uint);
}
template <>
std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_volcanite_raw(std::string path, bool allowCast) {
    return allowCast ? load_volcanite_raw_with_cast_<uint32_t>(path, "uint32", vk::Format::eR32Uint) : load_volcanite_raw_<uint32_t>(path, "uint32", 32, vk::Format::eR32Uint);
}
template <>
std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_volcanite_raw(std::string path, bool allowCast) {
    return allowCast ? load_volcanite_raw_with_cast_<uint16_t>(path, "uint16", vk::Format::eR16Uint) : load_volcanite_raw_<uint16_t>(path, "uint16", 16, vk::Format::eR16Uint);
}
template <>
std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_volcanite_raw(std::string path, bool allowCast) {
    return allowCast ? load_volcanite_raw_with_cast_<uint8_t>(path, "uint8", vk::Format::eR8Uint) : load_volcanite_raw_<uint8_t>(path, "uint8", 8, vk::Format::eR8Uint);
}

template <typename T>
void write_volcanite_raw_(std::string url, const Volume<T> *volume, std::string formatLabel) {
    if (volume == nullptr || volume->size() == 0)
        throw std::runtime_error("volume is empty or does not exist");

    std::ofstream vraw(url, std::ios_base::out | std::ios_base::binary);
    if (!vraw.is_open()) {
        std::ostringstream err;
        err << "unable to open Volcanite RAW file at: " << url << "\n";
        Logger(Error) << err.str();
        throw std::runtime_error(err.str());
    }

    // write header
    std::string line = std::to_string(volume->dim_x) + " " + std::to_string(volume->dim_y) + " " + std::to_string(volume->dim_z);
    vraw << line << std::endl;
    vraw << formatLabel << std::endl;
    // write binary data
    vraw.write(volume->getRawData_const(), volume->memorySize());

    vraw.close();
}

template <>
void Volume<uint8_t>::write_volcanite_raw(std::string url) {
    write_volcanite_raw_(std::move(url), this, "uint8");
}
template <>
void Volume<uint16_t>::write_volcanite_raw(std::string url) {
    write_volcanite_raw_(std::move(url), this, "uint16");
}
template <>
void Volume<uint32_t>::write_volcanite_raw(std::string url) {
    write_volcanite_raw_(std::move(url), this, "uint32");
}
template <>
void Volume<uint64_t>::write_volcanite_raw(std::string url) {
    write_volcanite_raw_(std::move(url), this, "uint64");
}

} // namespace vvv
