//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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

#include "volcanite/compression/encoder/CSGVBrickEncoder.hpp"
#include "volcanite/compression/VolumeCompressionBase.hpp"
#include "volcanite/compression/memory_mapping.hpp"

#include "csgv_constants.incl"
#include "vvv/util/util.hpp"

namespace volcanite {

// a little table to help you keep track of all these gruesome variable names:
//      child_index the index of the child of a paraent in 0 - 7
//      lod_dim     the number of voxels in each dimension of the current LOD of a brick
//      lod_width   the step size of the current LOD brick entries in each dimension measured in voxels of the finest LOD
//      index_step  the step size between output voxels in the current LOD as a number of morton indices, considering that one step forward equals one voxel step in the finest LOD
uint32_t CSGVBrickEncoder::valueOfNeighbor(const MultiGridNode *grid, const MultiGridNode *parent_grid,
                                           const glm::uvec3 &brick_pos, const uint32_t child_index,
                                           const uint32_t lod_dim, const uint32_t brick_size,
                                           const int neighbor_i) {
    assert(lod_dim > 0);
    assert(child_index >= 0 && child_index < 8);
    // find the position of the neighbor
    glm::ivec3 neighbor_pos = glm::ivec3(brick_pos) + neighbor[child_index][neighbor_i];
    if (glm::any(glm::lessThan(neighbor_pos, glm::ivec3(0))) ||
        glm::any(glm::greaterThanEqual(neighbor_pos, glm::ivec3(static_cast<int>(lod_dim)))))
        return INVALID;

    // in case we want to access a neighbor that is not already existing on this level (neighbor_i > our_i or any element of neighbor[child_index][neighbor_i] is positive,
    // we have to look up the parent element.
    else if (glm::any(glm::greaterThan(neighbor[child_index][neighbor_i], glm::ivec3(0)))) {
        // technically, this computes the index on a wrong level of detail (if not in the finest one), but because Z-order is self-including, it works
        return parent_grid[voxel_pos2idx(glm::ivec3(brick_pos / 2u) + neighbor[child_index][neighbor_i],
                                         glm::uvec3(lod_dim / 2))]
            .label;
    }
    // otherwise, lookup the neighbor
    else {
        return grid[voxel_pos2idx(neighbor_pos, glm::uvec3(lod_dim))].label;
    }
}

} // namespace volcanite