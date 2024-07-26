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

#include "vvv/pointren/PointCloud.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <random>
#include <vector>

#include "vvv/util/util.hpp"
#include "vvv/util/Logger.hpp"

void vvv::PointCloud::addPoints(const std::vector<glm::vec4> &points) {
    std::lock_guard<std::mutex> guard(m_mutex);

    size_t size = m_pos.size();
    m_pos.resize(size + points.size());
    #pragma omp parallel for default(none) shared(points, size)
    for(int i=0; i < points.size(); i++) {
        m_pos.at(size + i) = points.at(i);
    }
    m_accelDirty = true;
}

void vvv::PointCloud::setPoints(const std::vector<glm::vec4>& points) {
    std::lock_guard<std::mutex> guard(m_mutex);

    m_pos = points;
    m_accelDirty = true;
}

void vvv::PointCloud::setAccelerationGrid(glm::vec3 min, glm::vec3 max, float voxelsize) {
    std::lock_guard<std::mutex> guard(m_mutex);

    m_accelMin = min;
    m_accelMax = max;
    m_accelSize = voxelsize;
    glm::ivec3 old_accelDim = m_accelDim;
    m_accelDim = glm::ceil((max - min) / voxelsize);
    m_accelDirty = true;
}

void vvv::PointCloud::updateAccelerationGrid() {
    //TODO(Max) with this simple mutex, we can't lock here because the method is called from other methods already owning the mutex

    if (!m_accelDirty || !hasAcceleration())
        return;
    m_accelDirty = false;

    MiniTimer timer;

    // we store the 1D index in the voxel grid with every original point index
    m_accelIndices.resize(m_pos.size());
    #pragma omp parallel for default(none) shared(m_accelIndices, m_pos, m_accelMin, m_accelMax, m_accelDim)
    for (int i = 0; i < m_pos.size(); i++) {
        m_accelIndices.at(i).first = pos2index1D(m_pos.at(i));
        m_accelIndices.at(i).second = i;
    }

    // store based on voxel indices (sorts by first key)
    std::sort(m_accelIndices.begin(), m_accelIndices.end());

    // create the voxel grid buffer which stores the first index and number of points in a grid
    delete[] m_accelGrid;
    m_accelGrid = new glm::uvec2[m_accelDim.x * m_accelDim.y * m_accelDim.z];

    // iterate over the indices and update the corresponding grid entries
    size_t voxel_id = 0;
    int voxel_start_idx = 0;
    for (int i = 0; i < m_accelIndices.size();) {
        // skip all empty voxels until we reach the next occupied one
        while (voxel_id < m_accelIndices.at(i).first) {
            m_accelGrid[voxel_id].x = 0;
            m_accelGrid[voxel_id].y = 0;
            voxel_id++;
        }

        // count the points inside this voxel (and skip them)
        for (voxel_start_idx = i; i < m_accelIndices.size() && m_accelIndices.at(i).first == voxel_id; i++);
        if(i < m_accelIndices.size()) {
            m_accelGrid[voxel_id].x = voxel_start_idx;
            m_accelGrid[voxel_id].y = i - voxel_start_idx;
            voxel_id++;
        }
    }
    // fill all remaining voxels with 0
    for (; voxel_id < m_accelDim.x * m_accelDim.y * m_accelDim.z; voxel_id++)
        m_accelGrid[voxel_id] = glm::uvec2(0, 0);

    Logger(DEBUG) << "Build point cloud acceleration structure for " << m_pos.size() << " points with dim " << m_accelDim.x << ", " << m_accelDim.y << ", " << m_accelDim.z << " in " << timer.elapsed() << "s";

    if(m_accelDirty)
        Logger(WARN) << "Point cloud data changed while the acceleration structure was rebuild! This may lead to undefined behavior.";
}

std::vector<int> vvv::PointCloud::nearestNeighbors() {
    assert(hasAcceleration());
    std::lock_guard<std::mutex> guard(m_mutex);
    // update acceleration grid if necessary
    updateAccelerationGrid();

    std::vector<int> nearestNeighbors(m_pos.size());
    int maxDim = glm::max(m_accelDim.x, glm::max(m_accelDim.y, m_accelDim.z));

    #pragma omp parallel for default(none) shared(nearestNeighbors, maxDim, m_pos)
    for(size_t pos_id=0; pos_id<m_pos.size(); pos_id++)
    {
        int minIdx = nearestNeighbor(m_pos[pos_id], true, -1.f, false);
        assert(minIdx != -1);
        nearestNeighbors[pos_id] = static_cast<int>(minIdx);
    }

    return nearestNeighbors;
}

// TODO(Max) move poisson fill method to the utils

// use that here to fill the whole space, then remove points that collide with existing data set points

/// Fills the spacce with positions based on Bridon's poisson disc sampling algorithm from "Fast Poisson disk sampling in arbitrary dimensions".
/// The positions are NOT yet added to the point cloud but returned as a vector.
std::vector<glm::vec4> vvv::PointCloud::poissonFill(float rejectionDist) {
    assert(hasAcceleration());

    MiniTimer t;

    // check if we can use the cache (same spatial size and rejectionDistance)
    auto targetPoissonDimDist = glm::vec4((m_accelMax - m_accelMin), rejectionDist);
    if(glm::any(glm::notEqual(targetPoissonDimDist, m_poissonCacheDimDist))) {
        m_poissonCacheDimDist = targetPoissonDimDist;

        // get ourselves a random device
        std::default_random_engine generator;
        std::uniform_real_distribution<float> rnd(0.f, 1.f);

        // use Bridon's algorithm to fill a list of poisson sampled points
        {
            const float rejectionDist2 = rejectionDist * rejectionDist;

            // generate a new acceleration grid with cell size rejectionDist. If the cell size is rejectionDist/sqrt(3), each cell can only contain one sample
            float poissonCellSize = rejectionDist / glm::sqrt(3.f);
            int voxelCheckRadius = static_cast<int>(glm::ceil(rejectionDist / poissonCellSize));
            glm::ivec3 poissonDim = glm::ivec3(glm::ceil(m_accelMax - m_accelMin) / poissonCellSize) + glm::ivec3(1);
            size_t poissonCellCount = static_cast<size_t>(poissonDim.x) * poissonDim.y * poissonDim.z;
            glm::vec4 points[poissonCellCount];
// mark all grid cells empty (i.e. set its w component to 0)
#pragma omp parallel for default(none) shared(poissonCellCount, points)
            for (int i = 0; i < poissonCellCount; i++)
                points[i].w = 0.f;

            // generate a list of active points (points with a neighborhood in which we can add new points)
            std::vector<size_t> active;

            size_t addedPointCount = 0;
            glm::vec4 pos;
            glm::ivec3 voxel;
            size_t voxel1D;

            // usually we would generate a random point and add it to the grid (note that we add m_accelMin to the points as late as possible) and active list:
            //        pos(glm::vec3(rnd(generator), rnd(generator), rnd(generator)) * (m_accelMax - m_accelMin), 1.f);
            //        voxel = glm::ivec3(pos / poissonCellSize);
            //        voxel1D = voxel.x + voxel.y * poissonDim.x + voxel.z * poissonDim.x * poissonDim.y;
            // but we want to distribute the points more evenly, so we stratify the sampling. The higher the width, the less initial samples are placed.
            int stratifiedWidth = poissonDim.x;
            for (voxel.z = (stratifiedWidth / 2); voxel.z < poissonDim.z; voxel.z += stratifiedWidth) {
                for (voxel.y = (stratifiedWidth / 2); voxel.y < poissonDim.y; voxel.y += stratifiedWidth) {
                    for (voxel.x = (stratifiedWidth / 2); voxel.x < poissonDim.x; voxel.x += stratifiedWidth) {
                        // generate random positions within the selected voxel
                        pos = {(static_cast<float>(voxel.x) + rnd(generator)) * poissonCellSize, (static_cast<float>(voxel.y) + rnd(generator)) * poissonCellSize,
                               (static_cast<float>(voxel.z) + rnd(generator)) * poissonCellSize, 1.f};
                        assert(pos == glm::clamp(pos, glm::vec4(0.f), glm::vec4((m_accelMax - m_accelMin), 1.f)));

                        bool cont = false;
#pragma omp parallel for default(none), shared(cont, addedPointCount, active, points, pos, rejectionDist)
                        for (int i = 0; i < addedPointCount; i++) {
                            if (cont)
                                continue;
                            if (glm::length(points[active.at(i)] - pos) < rejectionDist) {
                                cont = true;
                            }
                        }
                        if (cont)
                            continue;

                        voxel1D = voxel.x + voxel.y * poissonDim.x + voxel.z * poissonDim.x * poissonDim.y;
                        points[voxel1D] = pos;
                        active.push_back(voxel1D);
                        addedPointCount++;
                    }
                }
            }

            // iterate over active points until all points are ruled out
            while (!active.empty()) {

                // choose a random point
                int randomIndex = static_cast<int>(rnd(generator) * static_cast<float>(active.size()));
                glm::vec4 selectedPoint = points[active[randomIndex]];
                assert(selectedPoint.w > 0);

                // generate k points in neighborhood between r and 2r and check if one of the k generated points can be added
                int k = 16;
                bool pointAdded = true;
                float startTheta = rnd(generator) * 2.f * glm::pi<float>();
                for (int k_i = 0; k_i < k; k_i++) {

                    pointAdded = true;

                    k = glm::max(k, static_cast<int>(glm::log2(addedPointCount / 8.f) * 8.f));

                    // generate random point in neighborhood [r, 2r]
                    float theta = ((k_i / 4) * 2.f / (k / 4) + rnd(generator) * 2.f / (k / 4)) * glm::pi<float>() + startTheta;
                    while (theta >= glm::pi<float>())
                        theta -= 2.f * glm::pi<float>();
                    glm::vec4 sphericalRnd(rnd(generator) * glm::pi<float>(), theta, rejectionDist + rejectionDist * rnd(generator), 1.f);
                    pos = selectedPoint + spherical2cartesian(sphericalRnd);
                    pos.w = 1.f;
                    pos = glm::clamp(pos, glm::vec4(0.f), glm::vec4(m_accelMax - m_accelMin, 1.f));
                    voxel = glm::ivec3(pos / poissonCellSize);
                    voxel1D = voxel.x + voxel.y * poissonDim.x + voxel.z * poissonDim.x * poissonDim.y;

                    // if the point is outside our grid we don't accept it..
                    if (glm::any(glm::lessThan(voxel, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxel, poissonDim))) {
                        pointAdded = false;
                        continue;
                    }
                    // .. or if our cell is already occupied (= the point doesn't fulfill the criterion because of how we selected the cell width)
                    if (points[voxel1D].w > 0) {
                        pointAdded = false;
                        continue;
                    }

                    // .. or if it is to close to an already added point
                    glm::ivec3 d;
                    for (int radius = 1; radius <= voxelCheckRadius; radius++) {
                        for (d.z = -radius; d.z <= radius; d.z++) {
                            for (d.y = -radius; d.y <= radius; d.y++) {
                                if (!pointAdded)
                                    break;
                                for (d.x = -radius; d.x <= radius; d.x++) {
                                    // only consider voxels at the "outer hull" of the radius block
                                    if ((d.z != -radius && d.z != radius) && (d.y != -radius && d.y != radius) && (d.x != -radius && d.x != radius))
                                        continue;

                                    if (glm::any(glm::lessThan(voxel + d, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxel + d, poissonDim)))
                                        continue;

                                    glm::vec4 check_point = points[voxel1D + d.x + d.y * poissonDim.x + d.z * poissonDim.x * poissonDim.y];
                                    if (check_point.w > 0 && glm::dot(glm::vec3(check_point - pos), glm::vec3(check_point - pos)) < rejectionDist2) {
                                        pointAdded = false;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // only add one of the points
                    if (pointAdded) {
                        points[voxel1D] = pos;
                        active.push_back(voxel1D);
                        addedPointCount++;
                        break;
                    }
                }

                if (!pointAdded) {
                    active.erase(active.begin() + randomIndex);
                }
            }

            // fill the cache
            m_poissonCache.resize(addedPointCount);
            size_t cache_index = 0;
            #pragma omp parallel for default(none) shared(poissonCellCount, points, addedPointCount, m_poissonCache, cache_index)
            for (size_t i = 0; i < poissonCellCount; i++) {
                if (points[i].w > 0.f) {
                    size_t loc;
                    #pragma omp atomic capture
                    {
                        loc = cache_index;
                        cache_index++;
                    }
                    m_poissonCache[loc] = points[i];
                }
            }
            assert(cache_index == m_poissonCache.size());
        }
    } // end update poissonCache

    // add points from the cache to the output list if they are not too close to an existing point
    std::lock_guard<std::mutex> guard(m_mutex);
    updateAccelerationGrid();

    std::vector<glm::vec4> out;
    out.reserve(m_poissonCache.size());
    #pragma omp parallel for default(none) shared(out, m_poissonCache, rejectionDist)
    for(size_t i=0; i<m_poissonCache.size(); i++) {
        if(nearestNeighbor(m_poissonCache.at(i), false, rejectionDist, true) < 0) {
            // sanity check: do we satisfy the distance criterion?
#if 0
            for(size_t n=0; n<m_poissonCache.size(); n++) {
                if(m_poissonCache.at(n).w > 0 && i != n && glm::length(points.at(n) - points.at(i)) < rejectionDist) {
                    Logger(ERROR) << "point " << str(points.at(i)) << " does not satisfy the poisson distance criterion";
                    break;
                }
            }
#endif
    #pragma omp critical
            { out.push_back(m_poissonCache.at(i)); }
        }
    }

    Logger(DEBUG) << "created " << out.size() << " poisson sampled points in " << t.elapsed() << "s";
    return out;
}

float vvv::PointCloud::kernelInterpolation(glm::vec3 pos, float radius, const std::vector<float>* pointAttributes) {
    assert(hasAcceleration());
    std::lock_guard<std::mutex> guard(m_mutex);
    updateAccelerationGrid();

    throw std::runtime_error("not implemented yet!");
}

/// Returns the nearest neighbor located to pos by querying the acceleration structure.
/// @param skipIdenticalPoint will not consider identical points, useful if the neighbor for a point in the data set is requested.
/// @param maxDistance neighbors outside this distance will not be considered
/// @return id of the nearest neighbor or -1 if no neighbor was found within maxDistance
int vvv::PointCloud::nearestNeighbor(glm::vec4 pos, bool skipIdenticalPoint, float maxDistance, bool stopOnFirstHit) {

    if(maxDistance < 0)
        maxDistance = (m_accelMax.x - m_accelMin.x) + (m_accelMax.y - m_accelMin.y) + (m_accelMax.z + m_accelMin.z);

    int maxDim = glm::max(m_accelDim.x, glm::max(m_accelDim.y, m_accelDim.z));

    float minDist2 = maxDistance * maxDistance;
    size_t minIdx = INT64_MAX;

    const glm::ivec3 startvoxel = pos2voxel(pos);
    glm::ivec3 voxel;
    // search for nearest neighbor of this point
    int maxRadius = glm::min(maxDim - 1, static_cast<int>(glm::ceil(maxDistance / m_accelSize)) + 1);
    for(int radius=0; radius <= maxRadius; radius++) {
        for(int dz = -radius; dz <= radius; dz++) {
            for(int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {

                    // only consider voxels at the "outer hull" of the radius block
                    if ((dz!=-radius && dz!=radius) && (dy!=-radius && dy!=radius) && (dx!=-radius && dx!=radius))
                        continue;

                    voxel = startvoxel + glm::ivec3(dx, dy, dz);
                    if (glm::any(glm::lessThan(voxel,glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxel, m_accelDim)))
                        continue;

                    // query acceleration structure voxel and iterate over index list of points within
                    auto entry = m_accelGrid[index1D(voxel)];
                    for(int j=entry.x; j < entry.x + entry.y; j++) {
                        size_t idx = m_accelIndices[j].second;
                        // the point itself should not be its nearest neighbor
                        if(skipIdenticalPoint && m_pos[idx].x == pos.x && m_pos[idx].y == pos.y && m_pos[idx].z == pos.z)
                            continue;

                        // if the (squared) distance to this point is shorter: update nearest neighbor
                        float dist2 = glm::dot(pos - m_pos[idx], pos - m_pos[idx]);
                        if(dist2 < minDist2) {
                            // only search one voxel grid radius further than the first found point
                            if(minIdx == INT64_MAX) {
                                maxRadius = radius + 1;
                            }
                            minDist2 = dist2;
                            minIdx = idx;

                            if(stopOnFirstHit)
                                return static_cast<int>(minIdx);
                        }
                    }
                }
            }
        }
    }

    if(minIdx == INT64_MAX)
        return -1;
    else
        return static_cast<int>(minIdx);
}
