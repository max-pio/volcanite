#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>
#include <vvv/util/Logger.hpp>

#include <cmath>
#ifdef LIB_HIGHFIVE
    #include <highfive/H5File.hpp>
#endif

namespace vvv {

template <typename T> std::shared_ptr<Volume<T>> load_volume_from_hdf5(std::string url, vk::Format gpuFormat) {
#ifdef LIB_HIGHFIVE
    HighFive::File file(url, HighFive::File::ReadOnly);
    auto dataset = file.getDataSet(file.getObjectName(0));

    // read dimension
    std::vector<size_t> dimensions = dataset.getDimensions();
    float max_dim = static_cast<float>(std::max(dimensions.at(0), std::max(dimensions.at(1), dimensions.at(2))));
    float physical_size_x = static_cast<float>(dimensions.at(0)) / max_dim;
    float physical_size_y = static_cast<float>(dimensions.at(1)) / max_dim;
    float physical_size_z = static_cast<float>(dimensions.at(2)) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y)|| !std::isfinite(physical_size_z)) {
        throw std::invalid_argument("invalid hdf5 physical volume size");
    }

    // allocate a memory region and read hdf5 object to it
    auto volume = std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, dimensions.at(0), dimensions.at(1), dimensions.at(2), gpuFormat, dimensions.at(0) * dimensions.at(1) * dimensions.at(2));
    dataset.read(volume->data().data());
    return volume;
#else
    throw std::runtime_error("HighFIVE / HDF5 libraries not found! Can not load .hdf5 volume file!");
    return nullptr;
#endif
}


template <> std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint32_t>(path, vk::Format::eR32Uint);
}
template <> std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint16_t>(path, vk::Format::eR16Uint);
}
template <> std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_hdf5(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for hdf5 volume loaders.");
    return load_volume_from_hdf5<uint8_t>(path, vk::Format::eR8Uint);
}

} // namespace vvv
