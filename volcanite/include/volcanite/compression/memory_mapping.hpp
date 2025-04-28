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

#pragma once

#include "vvv/util/space_filling_curves.hpp"

using namespace vvv;

namespace volcanite {

/// @brief converts a 3D index of a voxel in a full volume into its 1D memory index.
[[nodiscard]] inline glm::uvec3 voxel_idx2pos(size_t i, const glm::uvec3 volume_dim) {
    assert(i < volume_dim.x * volume_dim.y * volume_dim.z);
    return sfc::Cartesian::i2p(i, volume_dim);
}
/// @brief converts a 1D memory index into its 3D index of a voxel in a full volume.
[[nodiscard]] inline size_t voxel_pos2idx(glm::uvec3 p, const glm::uvec3 volume_dim) {
    assert(glm::all(glm::lessThan(p, volume_dim)));
    return sfc::Cartesian::p2i(p, volume_dim);
}

/// @brief converts a 3D index of a brick into its 1D memory index.
[[nodiscard]] inline uint32_t brick_pos2idx(glm::uvec3 brick_pos, const glm::uvec3 brick_count) {
    return sfc::Cartesian::p2i(brick_pos, brick_count);
}
/// @brief converts a 1D memory index into a 3D index of its brick.
[[nodiscard]] inline glm::uvec3 brick_idx2pos(uint32_t brick_index, const glm::uvec3 brick_count) {
    assert(brick_index < brick_count.x * brick_count.y * brick_count.z);
    return sfc::Cartesian::i2p(brick_index, brick_count);
}

/// @brief converts a 1D memory index of a voxel within a brick into a 3D voxel position within the brick.
/// Because of how we encode the LODs, this enumeration is required to always be in an "octree manner".
/// Iterating over it with a step size of 2*2*2=8 should land on all start points of 2x2x2 bricks in the Octree and so on.
/// Morton and Hilbert curves for example satisfy this criterion.
[[nodiscard]] inline glm::uvec3 enumBrickPos(uint32_t i) {
    // TODO: rename enumBrickPos to cache_idx2dpos or rename both to brickvoxel_idx2pos
    return sfc::Morton3D::i2p(i);
}
/// @brief converts a 1D memory index of a voxel within a brick into a 3D voxel position within the brick.
[[nodiscard]] inline glm::uint32_t indexOfBrickPos(const glm::uvec3 &p) {
    // TODO: rename indexOfBrickPos to cache_pos2idx or rename both to brickvoxel_pos2idx
    return sfc::Morton3D::p2i(p);
}

} // namespace volcanite