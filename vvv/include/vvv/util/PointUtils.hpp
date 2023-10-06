#pragma once

#include "vvv/util/Logger.hpp"
#include <glm/glm.hpp>
#include <chrono>
#include <vector>

namespace vvv
{

class PointUtil {
public:

    /**
     * Returns a space filling point sets where all points are aligned on a grid with spacing gridSpacing.
     * @param size the maximum position of points
     * @param gridSpacing the grid spacing between points
     * @param stratified if true, the positions are randomized within grid cells
     * @return a grid aligned point set
     */
    static std::vector<glm::vec4> fillGrid(const glm::vec3 size, float gridSpacing, bool stratified=false);

    /**
     * Returns a space filling point set where all points are not closer than rejectionDist to their nearest neighbors.
     * @param size the maximum positions of points
     * @param rejectionDist the minimum distance between points
     * @return a poisson disc sampled point set
     */
    static std::vector<glm::vec4> fillPoisson(const glm::vec3 size, float rejectionDist);


    /**
    * For every point in a, returns the index of the nearest neighbor in b.
    * @param a Points for which nearest neighbor is searched.
    * @param b Potential nearest neighbors for points in a.
    * @param skipIdenticalIDs if point pairs with the same index are ignored. Handy if you want to compute the nearest neighbors within the same point cloud.
    */
    template<glm::length_t L>
    static std::vector<size_t> nearestNeighbors(const std::vector<glm::vec<L, float, glm::defaultp>>& a, const std::vector<glm::vec<L, float, glm::defaultp>>& b, bool skipIdenticalIDs);

    /**
     * For every point in a, returns the indices of the k nearest neighbors in b.
     * @param a Points for which nearest neighbor is searched.
     * @param b Potential nearest neighbors for points in a.
     * @param k Number of nearest neighbors to be returned per point.
     * @param skipIdenticalIDs if point pairs with the same index are ignored. Handy if you want to compute the nearest neighbors within the same point cloud.
     */
    template<glm::length_t L>
    static std::vector<std::vector<size_t>> kNearestNeighbors(const std::vector<glm::vec<L, float, glm::defaultp>>& a, const std::vector<glm::vec<L, float, glm::defaultp>>& b, int k = 1, bool skipIdenticalIDs = false);

    /**
     * Transforms all points with the given transformation matrix in place.
     */
    static void transformInPlace(std::vector<glm::vec3>& points, const glm::mat4& transformation);

    /**
     * Transforms all points with the given transformation matrix in place.
     */
    static void transformInPlace(std::vector<glm::vec4>& points, const glm::mat4& transformation);
    /**
     * Copies a new list of positions from a list of indices in another position list. The given position list is not altered.
     */
    template<glm::length_t L>
    static std::vector<glm::vec<L, float, glm::defaultp>> indexToPositions(const std::vector<size_t>& indices, const std::vector<glm::vec<L, float, glm::defaultp>>& position_in);
};

}


// templated methods

/*
 * ATTENTION! Usually you should use the pointren/PointCloud class for computing nearest neighbors and other point cloud operations because it uses an acceleration structure that is
 * significantly faster
 */

template <glm::length_t L>
std::vector<size_t> vvv::PointUtil::nearestNeighbors(const std::vector<glm::vec<L, float, glm::defaultp>> &a, const std::vector<glm::vec<L, float, glm::defaultp>> &b, bool skipIdenticalIDs) {
    auto nn = kNearestNeighbors(a, b, 1, skipIdenticalIDs);
    std::vector<size_t> out(nn.size());
#pragma omp parallel for default(none)
    for(auto i = 0; i < nn.size(); i++)
        out[i] = nn[i][0];
    return out;
}


template<glm::length_t L>
std::vector<std::vector<size_t>> vvv::PointUtil::kNearestNeighbors(const std::vector<glm::vec<L, float, glm::defaultp>> &a, const std::vector<glm::vec<L, float, glm::defaultp>> &b, int k, bool skipIdenticalIDs) {
    assert(k > 0 && (k <= b.size() - (skipIdenticalIDs ? 1 : 0)));

    std::vector<std::vector<size_t>> out(a.size(), std::vector<size_t>(k));

    // iterate over all points in a ...
    #pragma omp parallel for default(none) shared(out, a, b, k, skipIdenticalIDs)
    for(size_t i = 0; i < a.size(); i++)
    {
        auto& kID = out[i]; // IDs of the present k nearest neighbors
        float kDist2[k];        // distances of the present k nearest neighbors
        float kMaxDist2 = -1.f; // maximum distance of one of the present k nearest neighbors
        int kIDMaxDist = 0;     // id of the element with the maximum distance in out
        int added = 0;          // number of points added so far

        // ... and find the k nearest neighbors in b
        for(size_t n = 0; n < b.size(); n++) {
            if(skipIdenticalIDs && n == i)
                continue;

            glm::vec3 grid_dist = a[i] - b[n];
            float dist2 = glm::dot(grid_dist, grid_dist);

            // the first k points are added in any case
            if(added < k) {
                kDist2[added] = dist2;
                kID[added] = n;
                if (dist2 > kMaxDist2)
                {
                    kMaxDist2 = dist2;
                    kIDMaxDist = added;
                }
                added++;
            }
            else if(dist2 < kMaxDist2)
            {
                // replace the previous neighbor with the highest distance with the new one
                kID[kIDMaxDist] = n;
                kDist2[kIDMaxDist] = dist2;
                // find the new highest distance of the current k neighbors
                kMaxDist2 = 0;
                for(int m = 0; m < k; m++)
                {
                    if(kDist2[m] > kMaxDist2)
                    {
                        kMaxDist2 = kDist2[m];
                        kIDMaxDist = m;
                    }
                }
            }
        }
    }

    return out;
}
