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

#include <mutex>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <stdexcept>

#include <vvv/util/Logger.hpp>

#include <vvv/core/Buffer.hpp>

namespace vvv {

struct PointCloudInfo {
    size_t pointCount;
    glm::vec3 accelerationGridMin;
    glm::vec3 accelerationGridMax;
    float accelerationGridCellSize;
    glm::ivec3 accelerationGridDim;
};

class PointCloud {

public:
    PointCloud() : m_pos(), m_mutex(),
                   m_accelDirty(true), m_accelGrid(nullptr), m_accelMin(0.f), m_accelMax(1.f), m_accelDim(0), m_accelSize(1.f),
                   m_poissonCacheDimDist(-1.f), m_poissonCache() {};

    ~PointCloud() {
        delete[] m_accelGrid;
    }

    // point set manipulation
    void clear() {
        m_pos.clear();
        m_accelIndices.clear();
        m_accelDirty = true;
    }
    void reserve(size_t size) { m_pos.reserve(size); }
    void push_back(const glm::vec4& p) { m_pos.push_back(p); m_accelDirty=true; }
    void setPoints(const std::vector<glm::vec4>& points);
    void addPoints(const std::vector<glm::vec4>& points);

    // access
    const std::vector<glm::vec4>& points() const { return m_pos; };
    size_t size() const { return m_pos.size(); }

    // acceleration structure
    /**
     * Configures the uniform grid used to accelerate queries.
     * @param min minimum point position covered by grid
     * @param max maximum point position covered by grid
     * @param voxelsize world space size in each dimension of grid voxels
     */
    void setAccelerationGrid(glm::vec3 min, glm::vec3 max, float voxelsize);
    bool hasAcceleration() { return m_accelDim.x * m_accelDim.y * m_accelDim.z > 0; }
    void setAccelerationDirty() { m_accelDirty = true; }
    /**
     * Updates the acceleration structure if it was marked dirty.
     */
    void updateAccelerationGrid();

    // utility functions
    /**
     * Returns the index of the nearest neighbor for every point in this point cloud.
     */
    std::vector<int> nearestNeighbors();
    std::vector<std::vector<int>> kNearestNeighbors(int k);

     /**
     * Returns a list of poisson sampled positions within the point clouds minimum and maximum dimensions. The positions are not yet added to the point cloud.
     */
     std::vector<glm::vec4> poissonFill(float rejectionDist);

     /**
      * Performs a epanechnikov kernel interpolation with the given kernel radius for the given point attributes.
      * If no point attributes are specified, the kernel density is returned.
      */
     float kernelInterpolation(glm::vec3 pos, float radius, const std::vector<float>* pointAttributes = nullptr);


     // vulkan objects
     PointCloudInfo getInfo() const { return PointCloudInfo{.pointCount=m_pos.size(), .accelerationGridMin=m_accelMin, .accelerationGridMax=m_accelMax, .accelerationGridCellSize=m_accelSize,
                                                            .accelerationGridDim=m_accelDim};}


     void uploadPositionsToBuffer(Buffer& buffer) const { /*assert(buffer contains vec4)*/ buffer.upload(m_pos); }
     /**
      * uploads map from 1D encoded acceleration grid cell to uvec2 buffer which points with (start index, point count) into the acceleration point index array
      */
     void uploadAccelerationGridToBuffer(Buffer& buffer) const {
         /*assert(buffer contains uvec2)*/
         assert(!m_accelDirty);
         buffer.upload(m_accelGrid, m_accelDim.x * m_accelDim.y * m_accelDim.z * sizeof(glm::uvec2));
     }
     /**
      * * uploads unsigned int map of indices of points in the position array ordered by 1D encoded acceleration grid cell
      */
     void uploadAccelerationIndicesToBuffer(Buffer& buffer) const {
         assert(!m_accelDirty);
         /*assert(buffer contains unsigned int)*/
         std::vector<uint32_t> tmp(m_accelIndices.size());
         #pragma omp parallel for default(none) shared(m_accelIndices, tmp)
         for(int i=0; i<m_accelIndices.size(); i++)
             tmp[i] = static_cast<uint32_t>(m_accelIndices[i].second);
         buffer.upload(tmp);
     }

     static std::shared_ptr<Buffer> createPositionStorageBuffer(GpuContextPtr ctx, size_t elemCapacity=4096, std::string label="point_cloud_pos") {
         return std::make_shared<Buffer>(ctx, BufferSettings{.label = std::move(label), .byteSize = elemCapacity * sizeof(glm::vec4), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
     }
     static std::shared_ptr<Buffer> createAccelerationGridBuffer(GpuContextPtr ctx, size_t elemCapacity=4096, std::string label="point_cloud_accel_grid") {
         return std::make_shared<Buffer>(ctx, BufferSettings{.label = std::move(label), .byteSize = elemCapacity * sizeof(glm::uvec2), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
     }
     static std::shared_ptr<Buffer> createAccelerationIndicesBuffer(GpuContextPtr ctx, size_t elemCapacity=4096, std::string label="point_cloud_accel_indices") {
         return std::make_shared<Buffer>(ctx, BufferSettings{.label = std::move(label), .byteSize = elemCapacity * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
     }


private:
    // the indexing determines in which order voxels of the grid are placed in 1D arrays, e.g. the memory buffer of the grid
    inline size_t index1D(const glm::ivec3& voxel) { return index1D(voxel.x, voxel.y, voxel.z); }
    size_t index1D(int voxel_x, int voxel_y, int voxel_z) {
        assert(voxel_x >= 0 && voxel_x < m_accelDim.x && voxel_y >= 0 && voxel_y < m_accelDim.y && voxel_z >= 0 && voxel_z < m_accelDim.z);
        return voxel_x + voxel_y*m_accelDim.x + voxel_z*m_accelDim.x*m_accelDim.y;
    }
    inline size_t pos2index1D(const glm::vec3& p) { return index1D(pos2voxel(p)); }
    inline size_t pos2index1D(const glm::vec4& p) { return pos2index1D(glm::vec3(p)); }
    inline glm::ivec3 pos2voxel(const glm::vec3& p) { return glm::clamp(glm::ivec3((p - m_accelMin)/m_accelSize), glm::ivec3(0), m_accelDim - 1); }
    inline glm::ivec3 pos2voxel(const glm::vec4& p) { return pos2voxel(glm::vec3(p)); }


    // some helper functions (could be made public, but have to consider mutex)
    int nearestNeighbor(glm::vec4 pos, bool skipIdenticalPos=false, float maxDistance=-1.f, bool stopOnFirstHit=false);

    // we have to prevent simultaneous changes to the point clouds, e.g. when rebuilding the acceleration structure
    std::mutex m_mutex;

    // poisson cache
    glm::vec4 m_poissonCacheDimDist;       ///< spatial size of the set (xyz) and rejection distance (w)
    std::vector<glm::vec4> m_poissonCache; ///< points spanning the spatial domain from the cached size (but which may be too close to a data set point)

    // point data
    std::vector<glm::vec4> m_pos;
    // acceleration structure
    bool m_accelDirty;
    glm::uvec2* m_accelGrid;    /// first: start index of points in the voxel in m_accelIndices, second: number of points inside this voxel
    std::vector<std::pair<size_t, size_t>> m_accelIndices;  /// first: voxel index in accel grid, second: index of point in m_pos
    glm::vec3 m_accelMin, m_accelMax;   // minimum and maximum world space positions in the grid
    glm::ivec3 m_accelDim;  /// x, y, and z dimension of the acceleration grid in voxels
    float m_accelSize;      /// size of one cell of the acceleration grid (in each dimension)
};


}