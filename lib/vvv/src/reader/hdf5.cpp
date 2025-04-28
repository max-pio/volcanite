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

#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>
#include <vvv/util/Logger.hpp>

#include <cmath>
#ifdef LIB_HIGHFIVE
#include <highfive/H5File.hpp>
#endif

namespace vvv {

template <typename T>
std::shared_ptr<Volume<T>> load_volume_from_hdf5(std::string url, vk::Format gpuFormat) {
#ifdef LIB_HIGHFIVE
    HighFive::File file(url, HighFive::File::ReadOnly);
    auto dataset = file.getDataSet(file.getObjectName(0));

    // read dimension
    std::vector<size_t> dimensions = dataset.getDimensions();
    float max_dim = static_cast<float>(std::max(dimensions.at(0), std::max(dimensions.at(1), dimensions.at(2))));
    float physical_size_x = static_cast<float>(dimensions.at(0)) / max_dim;
    float physical_size_y = static_cast<float>(dimensions.at(1)) / max_dim;
    float physical_size_z = static_cast<float>(dimensions.at(2)) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y) || !std::isfinite(physical_size_z)) {
        throw std::invalid_argument("invalid hdf5 physical volume size");
    }

    // allocate a memory region and read hdf5 object to it
    auto volume = std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, dimensions.at(0), dimensions.at(1), dimensions.at(2), gpuFormat, dimensions.at(0) * dimensions.at(1) * dimensions.at(2));
    dataset.read(volume->data().data());
    return volume;
#else
    throw std::runtime_error("HighFIVE / HDF5 libraries not found! Cannot load .hdf5 volume file!");
    return nullptr;
#endif
}

template <>
std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint32_t>(path, vk::Format::eR32Uint);
}
template <>
std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint16_t>(path, vk::Format::eR16Uint);
}
template <>
std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint8_t>(path, vk::Format::eR8Uint);
}

template <typename T>
void write_hdf5_(Volume<T> *volume, const std::string &path) {
#ifdef LIB_HIGHFIVE
    HighFive::File file(path, HighFive::File::ReadWrite | HighFive::File::Create | HighFive::File::Truncate);
    const std::string datasetName = "decompressed_volume_data";
    if (file.exist(datasetName))
        file.unlink(datasetName);

    auto dim = std::vector<size_t>{volume->dim_x, volume->dim_y, volume->dim_z};

    // rewrite volume data s.t. it is a 3D vector
    std::vector<std::vector<std::vector<T>>> tmp_volume_data(dim[0], std::vector<std::vector<T>>(dim[1], std::vector<T>(dim[2])));

    for (size_t z = 0; z < dim[0]; ++z) {
        for (size_t y = 0; y < dim[1]; ++y) {
            for (size_t x = 0; x < dim[2]; ++x) {
                size_t index = z * dim[1] * dim[2] + y * dim[2] + x;
                tmp_volume_data[z][y][x] = volume->data()[index];
            }
        }
    }

    HighFive::DataSetCreateProps probs;
    probs.add(HighFive::Chunking({std::min(dim[0], size_t{128}),
                                  std::min(dim[1], size_t{128}),
                                  std::min(dim[2], size_t{128})}));
    probs.add(HighFive::Deflate(9));

    auto dataset = file.createDataSet<T>(datasetName, HighFive::DataSpace(dim), probs);
    dataset.write(tmp_volume_data);
#else
    throw std::runtime_error("HighFIVE / HDF5 libraries not found! Cannot write volume to .hdf5 file!");
#endif
}

template <>
void Volume<uint32_t>::write_hdf5(const std::string &path) {
    write_hdf5_<uint32_t>(this, path);
}
template <>
void Volume<uint16_t>::write_hdf5(const std::string &path) {
    write_hdf5_<uint16_t>(this, path);
}
template <>
void Volume<uint8_t>::write_hdf5(const std::string &path) {
    write_hdf5_<uint8_t>(this, path);
}
} // namespace vvv
