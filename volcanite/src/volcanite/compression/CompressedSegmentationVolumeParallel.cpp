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

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include "volcanite/compression/pack_nibble.hpp"
#include "volcanite/compression/pack_wavelet_matrix.hpp"

namespace volcanite {

void CompressedSegmentationVolume::parallelDecompressLOD(int target_lod, std::vector<uint32_t> &out) const {
    if (!m_random_access)
        throw std::runtime_error("Parallel decompression requires previous compression with random access enabled.");

    const glm::uvec3 brickCount = getBrickCount();
    uint32_t inv_lod = getLodCountPerBrick() - 1u - target_lod;
    assert(inv_lod >= 0);

    glm::uvec3 brick_pos;
#ifndef NO_BRICK_DECODE_INDEX_REMAP
    std::vector<uint32_t> brick_cache(m_brick_size * m_brick_size * m_brick_size); // brick output in morton order
#endif

    // we iterate over all bricks and decompress brick voxels in parallel
    for (brick_pos.z = 0; brick_pos.z < brickCount.z; brick_pos.z++) {
        for (brick_pos.y = 0; brick_pos.y < brickCount.y; brick_pos.y++) {
            for (brick_pos.x = 0; brick_pos.x < brickCount.x; brick_pos.x++) {
                size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
#ifndef NO_BRICK_DECODE_INDEX_REMAP
                // decode brick with threads parallelizing over the output voxels
                m_encoder->parallelDecodeBrick(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                                               brick_cache.data(),
                                               glm::clamp(m_volume_dim - brick_pos * m_brick_size,
                                                          glm::uvec3(0u), glm::uvec3(m_brick_size)),
                                               static_cast<int>(inv_lod));
                // fill output array with decoded brick entries
                for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
                    glm::uvec3 out_pos = brick_pos * m_brick_size + enumBrickPos(i);
                    if (glm::all(glm::lessThan(out_pos, m_volume_dim))) {
                        out[voxel_pos2idx(out_pos, m_volume_dim)] = brick_cache[i];
                    }
                }
#else
                parallelDecodeBrick(brick_idx, &(out[pos2idx(brick_pos * m_brick_size, m_volume_dim)]), glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inv_lod);
#endif
            }
        }
    }
}

} // namespace volcanite
