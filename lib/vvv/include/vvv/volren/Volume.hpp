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
#include <memory>
#include <string>
#include <vector>

#include "vvv/core/Texture.hpp"
#include "vvv/core/preamble.hpp"

namespace vvv {

namespace detail {
/// check if string `a` ends with `b`
static bool EndsWith(const std::string &a, const std::string &b) {
    if (b.size() > a.size())
        return false;
    return std::equal(a.begin() + a.size() - b.size(), a.end(), b.begin());
}
}; // namespace detail

const vk::Format DeviceFormatDontCare = static_cast<vk::Format>(-1);

template <typename ElementType = uint16_t>
struct RangeLimits {
    ElementType minValue;
    ElementType maxValue;
    float minGrad; //<! gradient magnitude
    float maxGrad;
};

/// Phyisical size determines the bounding box of the volume, while the dimensions determines the number of data samples
/// along each axis within the volume.
///
/// The volume data is layed out in [z][y][x] order, meaning the x-axis is coalesced/varies fastest, z has the largest stride and varies slowest.
/// The logical data layout uses a right-handed coordinate system where z is the UP axis (height), y the depth and x the width.
template <typename ElementType = uint16_t, typename HolderType = std::vector<ElementType>>
class Volume {

    // possible extension:
    // accessor via [] a la vector
    // ElementType independent variant. Currently problematic to read a format that can hold multiple types, e.g. NRRD.
    //   could design this independent with typed views
    // compatibility with eigen/numpy
    // decouple CPU and GPU memory. e.g. add a place a la torch, or make it possible to drop a location within the class
    // abstract read-write routines in a way that allows them to be used in Texture, Volume, ...

  public:
    /// Load volumetric data from a open microscopy TIFF file.
    ///
    /// This loader is not standard conformat and only supports the standard
    /// subset required to load the data produced by the in-house built Light
    /// Sheet Microscopes of Neinhaus' Group at the Karlsruhe Institute of
    /// Technology.
    static std::shared_ptr<Volume<ElementType, HolderType>> load_ome_tiff(std::string path) { throw std::runtime_error("element holder type combination unsupported for TIFF"); }

    /// A non-standard conformant NRRD reader that is able to read files from https://klacansky.com/open-scivis-datasets/
    /// @param allowCast By default, an error is thrown if the volume component type and the component type stored in the file mismatch. If set to true, a conversion is attempted instead.
    static std::shared_ptr<Volume<ElementType, HolderType>> load_nrrd(std::string path, bool allowCast = true); // { throw std::runtime_error("element holder type combination unsupported for NRRD"); }
    void write_nrrd(const std::string &path, bool separatePayloadFile = true);

    /// An even more simplified nrrd format for the cellsinsilico volume data that Max hands out to students. Format is: one line "dim_x dim_y dim_z" and one line data type "uint[8|16|32]" followed by payload.
    static std::shared_ptr<Volume<ElementType, HolderType>> load_volcanite_raw(std::string path, bool allowCast = false);
    void write_volcanite_raw(std::string path);

    /// Hdf5 file which is expected to have a 3D array as its first root object which will be loaded as the volume
    static std::shared_ptr<Volume<ElementType, HolderType>> load_hdf5(std::string path, bool allowCast = false);
    void write_hdf5(const std::string &path);

    /// vti file format from the vtk library, but we expect a very precise format: an ImageData file
    static std::shared_ptr<Volume<ElementType, HolderType>> load_vti(std::string path, bool allowCast = false);
    // static void write_vti(std::string path);

    static std::shared_ptr<Volume<ElementType, HolderType>> load(std::string filepath) {
        if (filepath.ends_with(".tiff")) {
            return Volume<ElementType, HolderType>::load_ome_tiff(filepath);
        } else if (filepath.ends_with(".nrrd")) {
            return Volume<ElementType, HolderType>::load_nrrd(filepath);
        } else if (filepath.ends_with(".hdf5") || filepath.ends_with(".h5")) {
            return Volume<ElementType, HolderType>::load_hdf5(filepath);
        } else if (filepath.ends_with(".vti")) {
            return Volume<ElementType, HolderType>::load_vti(filepath);
        } else if (filepath.ends_with(".vraw") || filepath.ends_with(".raw")) {
            return Volume<ElementType, HolderType>::load_vti(filepath);
        } else {
            throw std::invalid_argument("unknown volume file extension for " + filepath);
        }
    }

    bool write(std::string filepath) {
        if (filepath.ends_with(".nrrd")) {
            Volume<ElementType, HolderType>::write_nrrd(filepath);
        } else if (filepath.ends_with(".hdf5") || filepath.ends_with(".h5")) {
            Volume<ElementType, HolderType>::write_hdf5(filepath);
        } else if (filepath.ends_with(".vraw") || filepath.ends_with(".raw")) {
            Volume<ElementType, HolderType>::write_volcanite_raw(filepath);
        } else {
            throw std::invalid_argument("unknown volume file extension for " + filepath);
            return false;
        }
        return true;
    }

    template <typename... Args>
    Volume(float physical_size_x, float physical_size_y, float physical_size_z, uint32_t dim_x, uint32_t dim_y, uint32_t dim_z, vk::Format format, Args &&...args)
        : physical_size_x(physical_size_x), physical_size_y(physical_size_y), physical_size_z(physical_size_z), dim_x(dim_x), dim_y(dim_y), dim_z(dim_z), format(format),
          m_payload(std::forward<Args>(args)...), m_texture(nullptr) {}

    ~Volume() { delete m_texture; }

    /// Get the single channel, uncompressed volumentric data slice in row major order
    char *getDataInRowMajorOrder() { return reinterpret_cast<char *>(m_payload.data()); }
    const char *getDataInRowMajorOrder_const() { return reinterpret_cast<const char *>(m_payload.data()); }

    /// Get the raw volume data. May be compressed, swizzled, etc.
    const char *getRawData_const() const { return reinterpret_cast<const char *>(m_payload.data()); }
    char *getRawData() { return reinterpret_cast<char *>(m_payload.data()); }

    size_t size() const { return dim_x * dim_y * dim_z; }
    size_t memorySize() const { return size() * sizeof(ElementType); }

    HolderType &data() { return m_payload; }
    const HolderType &dataConst() const { return m_payload; }

    inline bool isElementInBounds(size_t x, size_t y, size_t z) const { return x < dim_x && y < dim_y && z < dim_z; }
    inline bool isElementInBounds(int x, int y, int z) const { return x < dim_x && y < dim_y && z < dim_z; }

    template <typename V>
    inline ElementType getElement(V x, V y, V z) const { return m_payload[z * (dim_x * dim_y) + y * dim_x + x]; }
    inline ElementType getElement(glm::uvec3 v) const { return getElement(v.x, v.y, v.z); }
    inline ElementType getElement(glm::ivec3 v) const { return getElement(v.x, v.y, v.z); }

    template <typename V>
    inline ElementType getElementClamped(V x, V y, V z) const {
        uint32_t cx = std::clamp(static_cast<uint32_t>(x), 0u, dim_x - 1);
        uint32_t cy = std::clamp(static_cast<uint32_t>(y), 0u, dim_y - 1);
        uint32_t cz = std::clamp(static_cast<uint32_t>(z), 0u, dim_z - 1);

        return m_payload[cz * (dim_x * dim_y) + cy * dim_x + cx];
    }
    inline ElementType getElementClamped(glm::uvec3 v) const { return getElementClamped(v.x, v.y, v.z); }
    inline ElementType getElementClamped(glm::ivec3 v) const { return getElementClamped(v.x, v.y, v.z); }

    template <typename V>
    inline glm::vec3 getGradient(V x, V y, V z) const {
        const auto gx = 0.5 * (static_cast<float>(getElementClamped(x + 1, y, z)) - static_cast<float>(getElementClamped(x - 1, y, z)));
        const auto gy = 0.5 * (static_cast<float>(getElementClamped(x, y + 1, z)) - static_cast<float>(getElementClamped(x, y - 1, z)));
        const auto gz = 0.5 * (static_cast<float>(getElementClamped(x, y, z + 1)) - static_cast<float>(getElementClamped(x, y, z - 1)));

        return glm::vec3(gx, gy, gz);
    }

    inline glm::vec3 getGradient(glm::uvec3 v) const { return getGradient(v.x, v.y, v.z); }
    inline glm::vec3 getGradient(glm::ivec3 v) const { return getGradient(v.x, v.y, v.z); }

    template <typename V>
    inline float getGradientMagnitude(V x, V y, V z) const { return glm::length(getGradient(x, y, z)); }
    inline float getGradientMagnitude(glm::uvec3 v) const { return getGradientMagnitude(v.x, v.y, v.z); }
    inline float getGradientMagnitude(glm::ivec3 v) const { return getGradientMagnitude(v.x, v.y, v.z); }

    RangeLimits<ElementType> getMinMax() const {
        ElementType min = std::numeric_limits<ElementType>::max();
        ElementType max = std::numeric_limits<ElementType>::min();
        float gradmin = std::numeric_limits<float>::max();
        float gradmax = std::numeric_limits<float>::min();

        for (uint32_t z = 0; z < dim_z; ++z) {
            for (uint32_t y = 0; y < dim_y; ++y) {
                for (uint32_t x = 0; x < dim_x; ++x) {
                    const auto value = getElement(x, y, z);
                    min = std::min(min, value);
                    max = std::max(max, value);
                    const auto grad = getGradientMagnitude(x, y, z);
                    gradmin = std::min(gradmin, grad);
                    gradmax = std::max(gradmax, grad);
                }
            }
        }

        RangeLimits<ElementType> limits;

        limits.minValue = min;
        limits.maxValue = max;
        limits.minGrad = gradmin;
        limits.maxGrad = gradmax;

        return limits;
    }

    inline void setElement(size_t x, size_t y, size_t z, ElementType v) { m_payload[z * (dim_x * dim_y) + y * dim_x + x] = v; }

    inline void setElement(int x, int y, int z, ElementType v) { m_payload[z * (dim_x * dim_y) + y * dim_x + x] = v; }

    bool isTextureInitialized() const { return m_texture != nullptr; }

    Texture *getTexture(vvv::GpuContextPtr ctx) {
        if (!isTextureInitialized())
            m_texture = new Texture::input3d(ctx, format, dim_x, dim_y, dim_z);
        return m_texture;
    }

    void deleteTexture() {
        delete m_texture;
        m_texture = nullptr;
    }

    glm::vec3 shape() const {
        return glm::vec3(dim_x, dim_y, dim_z);
    }

    glm::vec3 physicalSize() const {
        return glm::vec3(physical_size_x, physical_size_y, physical_size_z);
    }

    void resize(uint32_t x, uint32_t y, uint32_t z, ElementType padding_element = 0) {
        HolderType new_payload(x * y * z, padding_element);
        for (int iz = 0; iz < dim_z; iz++) {
            for (int iy = 0; iy < dim_y; iy++) {
                for (int ix = 0; ix < dim_x; ix++) {
                    if (ix < x && iy < y && iz < z)
                        new_payload[iz * (x * y) + iy * x + ix] = getElement(ix, iy, iz);
                }
            }
        }
        m_payload = new_payload;
        dim_x = x;
        dim_y = y;
        dim_z = z;
        deleteTexture();
    }

    void setPayload(uint32_t x, uint32_t y, uint32_t z, HolderType &data) {
        dim_x = x;
        dim_y = y;
        dim_z = z;
        m_payload = data;
    }

  protected:
    HolderType m_payload;
    Texture *m_texture;

  public:
    float physical_size_x, physical_size_y, physical_size_z;
    uint32_t dim_x, dim_y, dim_z;

    vk::Format format;
};

template <typename T>
struct HomogenousCube : public Volume<T> {
    HomogenousCube(uint32_t dim_x, uint32_t dim_y, uint32_t dim_z, T payload, vk::Format format) : Volume<T>(1, 1, 1, dim_x, dim_y, dim_z, format, dim_x * dim_y * dim_z, payload) {}
    HomogenousCube(T payload, vk::Format format) : HomogenousCube(1, 1, 1, payload, format) {}
};

}; // namespace vvv
