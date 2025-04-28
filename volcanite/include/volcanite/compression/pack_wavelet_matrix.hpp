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

#include "volcanite/compression/wavelet_tree/BitVector.hpp"
#include <glm/glm.hpp>

namespace volcanite {

inline uint32_t getFlatRankEntries(uint32_t bit_vector_size) { return bit_vector_size / BV_L1_BIT_SIZE + 1u; }

// TODO: better struct padding, move uvec4 and BV_L12Type to front
struct WMBrickHeader {
    uint32_t palette_size;        ///< only included here as padding
    uint32_t text_size;           ///< symbols in the encoding stream
    glm::uvec4 ones_before_level; ///< number of ones before each level in the wavelet matrix
    glm::uvec4 zeros_on_level;    ///< number of zeros within each level in the wavelet matrix
    const BV_L12Type fr[1];       ///< L12 flat rank acceleration structure (flexible array member)

    WMBrickHeader(const WMBrickHeader &) = delete; // copying is not allowed because of the flexible array member
};
static_assert(sizeof(WMBrickHeader) == 4 * 12, "WMBrickHeader must be tightly packed.");

/// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
/// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
/// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
/// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
/// The first 4 bit element start4bit must be the first position in a 32bit memory location.
/// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
/// v[0] = 0 being the start operation count of the first LOD.
/// @return the new end4bit endpoint measured in number of 4 bit elements
uint32_t packWaveletMatrix(uint32_t *v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count);

// WAVELET MATRIX ACCESS AND RANK ==================================================================================

uint32_t wm_access(uint32_t position, const WMBrickHeader *wm_header, const BV_WordType *bit_vector);
uint32_t wm_rank(uint32_t position, uint32_t symbol, const WMBrickHeader *wm_header, const BV_WordType *bit_vector);

// ===============================================================================================================//
//                                          HUFFMAN WAVELET MATRIX                                                //
// ===============================================================================================================//

// TODO: better struct padding, move uvec4 and BV_L12Type to front
struct WMHBrickHeader {
    uint32_t bit_vector_size;       ///< total number of bits in all concatenated bit vectors (not the text_size!)
    uint32_t ones_before_level[5];  ///< number of ones before each level in the wavelet matrix
    glm::uvec4 level_starts_1_to_4; ///< bit vector level starts for levels 1,2,3, and 4. L0 is always 0, L5 undef.
    const BV_L12Type fr[1];         ///< L12 flat rank acceleration structure (flexible array member)

    WMHBrickHeader(const WMHBrickHeader &) = delete; // copying is not allowed because of the flexible array member
};

static_assert(sizeof(WMHBrickHeader) == 4 * 12, "WMHBrickHeader must be tightly packed.");

/// If the encoding uses stop bits, the lookup positions for a multi-grid node (inv_lod, inv_lod_op_i) in the
/// encoding stream of the current level-of-detail (LOD) may change:\n
/// 1. if any (grand-)parent sets a stop bit, the node is not present and that (grand-)parent should be accessed
/// instead.\n
///  2. the lookup position within the current LOD is moved to the front if any previous nodes in this level have
/// one or more (grand-)parents that set a stop bit.\n
/// This method takes care of these changes.
/// For case 1, the input argument references inv_lod and inv_lod_op_i are updated in place to refer to the parent.
/// Additionally, the encoding index for the lookup the corresponding node *after these changes* is returned as
/// inv_lod_starts[inv_lod] + inv_lod_op_i - offset.
/// @return the index to access the possibly changed node index (inv_lod, inv_lod_op_i)
uint32_t getEncodingIndexWithStopBits(uint32_t &inv_lod, uint32_t &inv_lod_op_i, const uint32_t *inv_lod_starts,
                                      const FlatRank_BitVector_ptrs &stop_bits);

/// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
/// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
/// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
/// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
/// The first 4 bit element start4bit must be the first position in a 32bit memory location.
/// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
/// v[0] = 0 being the start operation count of the first LOD.
/// @return the new end4bit endpoint measured in number of 4 bit elements
uint32_t packWaveletMatrixHuffman(uint32_t *v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count);

// WAVELET MATRIX ACCESS AND RANK ==================================================================================

uint32_t wm_huffman_access(uint32_t position, const WMHBrickHeader *wm_header, const BV_WordType *bit_vector);
uint32_t wm_huffman_rank(uint32_t position, uint32_t symbol, const WMHBrickHeader *wm_header, const BV_WordType *bit_vector);

} // namespace volcanite