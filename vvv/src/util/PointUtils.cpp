#include "vvv/util/PointUtils.hpp"

#include "vvv/util/util.hpp"
#include <random>
#include <glm/gtc/constants.hpp>

void vvv::PointUtil::transformInPlace(std::vector<glm::vec3> &points, const glm::mat4 &transformation) {
#pragma omp parallel for default(none) shared(points, transformation)
    for(auto it = points.begin(); it != points.end(); it++)
        *it = glm::vec3(transformation * glm::vec4(*it, 1.f));
}

void vvv::PointUtil::transformInPlace(std::vector<glm::vec4> &points, const glm::mat4 &transformation) {
#pragma omp parallel for default(none) shared(points, transformation)
    for(int i =0; i < points.size(); i++)
        points[i] = transformation * points[i];
}

template <glm::length_t L>
std::vector<glm::vec<L, float, glm::defaultp>> vvv::PointUtil::indexToPositions(const std::vector<size_t> &indices, const std::vector<glm::vec<L, float, glm::defaultp>> &position_in) {
    std::vector<glm::vec<L, float, glm::defaultp>> out(indices.size());
#pragma omp parallel for default(none)
    for(auto i = 0; i < indices.size(); i++)
        out[i] = position_in[indices[i]];
    return out;
}

std::vector<glm::vec4> vvv::PointUtil::fillGrid(const glm::vec3 size, float gridSpacing, bool stratified) {
    glm::ivec3 gridSize = glm::ivec3(size/gridSpacing);
    std::vector<glm::vec4> out(gridSize.x * gridSize.y * gridSize.z);
    if(!stratified) {
        #pragma omp parallel for default(none) shared(gridSize, gridSpacing, stratified, out)
        for (int z = 0; z < gridSize.z; z++) {
            for (int y = 0; y < gridSize.y; y++) {
                for (int x = 0; x < gridSize.x; x++) {
                    out[x + y * gridSize.x + z * gridSize.x * gridSize.y] = {(static_cast<float>(x) + 0.5f) * gridSpacing,
                                                                             (static_cast<float>(y) + 0.5f) * gridSpacing,
                                                                             (static_cast<float>(z) + 0.5f) * gridSpacing,
                                                                             1.f};
                }
            }
        }
    }
    else {
        std::default_random_engine generator;
        std::uniform_real_distribution<float> rnd(0.f, 1.f);
        #pragma omp parallel for default(none) shared(gridSize, gridSpacing, stratified, rnd, generator, out)
        for (int z = 0; z < gridSize.z; z++) {
            for (int y = 0; y < gridSize.y; y++) {
                for (int x = 0; x < gridSize.x; x++) {
                    out[x + y * gridSize.x + z * gridSize.x * gridSize.y] = {(static_cast<float>(x) + rnd(generator)) * gridSpacing,
                                                                             (static_cast<float>(y) + rnd(generator)) * gridSpacing,
                                                                             (static_cast<float>(z) + rnd(generator)) * gridSpacing,
                                                                             1.f};
                }
            }
        }
    }
    return out;
}

std::vector<glm::vec4> vvv::PointUtil::fillPoisson(glm::vec3 size, float rejectionDist) {
    // get ourselves a random device
    std::default_random_engine generator(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<float> rnd(0.f, 1.f);

    // We use Bridon's algorithm to fill a list of poisson sampled points
    const float rejectionDist2 = rejectionDist * rejectionDist;
    // generate a new acceleration grid with cell size rejectionDist. If the cell size is rejectionDist/sqrt(3), each cell can only contain one sample
    float poissonCellSize = rejectionDist / glm::sqrt(3.f);
    int voxelCheckRadius = static_cast<int>(glm::ceil(rejectionDist / poissonCellSize));
    glm::ivec3 poissonDim = glm::ivec3(glm::ceil(size / poissonCellSize)) + glm::ivec3(1);
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
                assert(pos == glm::clamp(pos, glm::vec4(0.f), glm::vec4(size, 1.f)));

                bool cont = false;
                #pragma omp parallel for default(none), shared(cont, addedPointCount, active, points, pos, rejectionDist)
                for (int i = 0; i < addedPointCount; i++) {
                    if (cont)
                        continue;
                    if (glm::length(points[active[i]] - pos) < rejectionDist)
                        cont = true;
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

            k = glm::max(k, static_cast<int>(glm::log2(static_cast<float>(addedPointCount) / 8.f) * 8.f));

            // generate random point in neighborhood [r, 2r]
            float theta = ((k_i / 4) * 2.f / (k / 4) + rnd(generator) * 2.f / (k / 4)) * glm::pi<float>() + startTheta; // we stratify theta to generate better samples
            while (theta >= glm::pi<float>())
                theta -= 2.f * glm::pi<float>();
            glm::vec4 sphericalRnd(rnd(generator) * glm::pi<float>(), theta, rejectionDist + rejectionDist * rnd(generator), 1.f);
            pos = selectedPoint + spherical2cartesian(sphericalRnd);
            pos.w = 1.f;
            pos = glm::clamp(pos, glm::vec4(0.f), glm::vec4(size, 1.f));
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

        // if we couldn't add any of the candidates for our selected index point, we assume it's neighborhood is crowded and remove it
        if (!pointAdded) {
            active.erase(active.begin() + randomIndex);
        }
    }

    // add remaining points to the output list
    std::vector<glm::vec4> out;
    out.reserve(addedPointCount);
    #pragma omp parallel for default(none) shared(out, poissonCellCount, points, rejectionDist)
    for(size_t i=0; i<poissonCellCount; i++) {
        if(points[i].w > 0.f) {
            #pragma omp critical
            { out.push_back(points[i]); }
        }
    }
    return out;
}