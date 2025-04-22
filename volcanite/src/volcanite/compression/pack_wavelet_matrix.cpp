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

#include "volcanite/compression/pack_wavelet_matrix.hpp"

#include "volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"

namespace volcanite {

// HELPER FUNCTIONS FOR BIT VECTOR ACCESS AND RANK =================================================================

inline uint32_t _bv_access(uint32_t index, const uint64_t *bv) {
    return bitfieldExtract(bv[index / BV_WORD_BIT_SIZE], static_cast<int>(index % BV_WORD_BIT_SIZE), 1);
}

uint32_t _fr_rank1(uint32_t index, const uint64_t *bv, const uint64_t *fr) {
    assert(getL1Entry(fr[0]) == 0u && "corrupted flat rank: first L1 is not 0");

    // ........ ........  bits
    // ┌┐┌┐┌┐┌┐ ┌┐┌┐┌┐┌┐  words
    // └┘└┘└┘└┘ └┘└┘└┘└┘
    // ┌──┐┌──┐ ┌──┐┌──┐  l2-blocks
    // └──┘└──┘ └──┘└──┘
    // ┌──────┐ ┌──────┐  l1-blocks
    // └──────┘ └──────┘

    // query L12 acceleration structure
    BV_L12Type l12 = fr[index / BV_L1_BIT_SIZE];
    uint32_t rank1_res = getL1Entry(l12);
    assert(rank1_res < (index == 0u ? 1u : index) && "_fr_rank1 getL1Entry return value too high.");
    rank1_res += getL2Entry(l12, (index % BV_L1_BIT_SIZE) / BV_L2_BIT_SIZE);

    // perform bit counts on a word level to count the remaining bits
    uint32_t offset = ((index / BV_WORD_BIT_SIZE) / BV_L2_WORD_SIZE) * BV_L2_WORD_SIZE;
    // fill missing 'full' counted words if L2-blocks cover multiple words
    if (BV_L2_WORD_SIZE > 1) {
        for (uint32_t _w = 0u; _w < ((index / BV_WORD_BIT_SIZE) % BV_L2_WORD_SIZE); _w++) {
            rank1_res += bitCount(bv[offset]);
            offset++;
        }
    }

    // if this is a rank(text_size) query, the inlining of the function lead to the potential out of bounds
    // access bv[offset] being ignored.
    assert(rank1_res + rank1Word(bv[offset], index % BV_WORD_BIT_SIZE) < (index == 0u ? 1u : index) && "_fr_rank1 return value too high");
    return rank1_res + rank1Word(bv[offset], index % BV_WORD_BIT_SIZE);
}

inline uint32_t _fr_rank0(uint32_t index, const uint64_t *bv, const uint64_t *fr) {
    return index - _fr_rank1(index, bv, fr);
}

// WAVELET MATRIX PACKING, ACCESS AND RANK =========================================================================

/// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
/// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
/// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
/// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
/// The first 4 bit element start4bit must be the first position in a 32bit memory location.
/// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
/// v[0] = 0 being the start operation count of the first LOD.
/// @return the new end4bit endpoint measured in number of 4 bit elements
uint32_t packWaveletMatrix(uint32_t *v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count) {
    // Construct a temporary WaveletMatrix Object from the input stream
    WaveletMatrix wm(v, start4bit, end4bit);

    // --- brick header extension ---
    uint32_t out_i = start4bit / 8u; // count in 32 bit instead of 4 bit elements
    // (overwrite header start indices, LOD 0) ?
    // uint32_t text size | 4x uint32 ones before level | 4x uint32 zeros in level

    v[out_i++] = wm.getTextSize();
    for (int _i = 0; _i < 4; _i++)
        v[out_i++] = wm.getOnesBeforeLevel()[_i];
    for (int _i = 0; _i < 4; _i++)
        v[out_i++] = wm.getZerosInLevel()[_i];

    // keep track of end4bit
    end4bit = out_i * 8;
    // update header so that v[0, .. LOD_COUNT) contain the operations count at which each LOD starts. This is a
    // constant offset on each LOD start so that v[0] = 0.
    uint32_t base_header_size = v[0];
    for (int _i = 0; _i < lod_count; _i++)
        v[_i] -= base_header_size;

    // ---- 64 bit encoding begins ----
    static_assert(sizeof(BV_WordType) == sizeof(BV_L12Type));
    BV_WordType *v64 = reinterpret_cast<uint64_t *>(&v[out_i]);
    out_i = 0u;

    // FlatRank
    const BV_L12Type *fr = wm.getFlatRank()->getRawData();
    for (uint32_t _i = 0u; _i < wm.getFlatRank()->getRawDataSize(); _i++) {
        v64[out_i++] = fr[_i];
    }
    // Bit Vector
    const BV_WordType *bv = wm.getBitVector()->getRawDataConst();
    for (uint32_t _i = 0u; _i < wm.getBitVector()->getRawDataSize(); _i++) {
        v64[out_i++] = bv[_i];
    }

    // return the new end4bit
    return end4bit + out_i * 16;
}

const WMBrickHeader *getWMBrickHeaderFromEncoding(const uint32_t *v, uint32_t base_header_size) {
    // to ensure tight packing, the WMBrickHeader starts one uint earlier in the previous (normal) brick header
    return reinterpret_cast<const WMBrickHeader *>(v + base_header_size - 1u);
}

const BV_WordType *getWMBitVectorFromEncoding(const uint32_t *v, uint32_t base_header_size) {
    return reinterpret_cast<const BV_WordType *>(v + base_header_size + 9 + (sizeof(BV_L12Type) / sizeof(uint32_t)) * getFlatRankEntries(v[base_header_size] * WM_LEVELS));
}

uint32_t wm_access(uint32_t position, const WMBrickHeader *wm_header, const BV_WordType *bit_vector) {
    // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::access()

    uint32_t result = 0u;
    bool bit = _bv_access(position, bit_vector);
    for (int level = 0; level < WM_LEVELS; ++level) {
        result <<= 1;
        size_t const ones_before = _fr_rank1(position, bit_vector, wm_header->fr) - wm_header->ones_before_level[level];
        if (bit) {
            result |= 1ULL;
            position =
                (level + 1) * wm_header->text_size + wm_header->zeros_on_level[level] + ones_before;
        } else {
            size_t const zeros_before =
                (position - (level * wm_header->text_size)) - ones_before;
            position = (level + 1) * wm_header->text_size + zeros_before;
        }
        if (level < WM_LEVELS - 1u)
            bit = _bv_access(position, bit_vector);
    }
    return result;
}

uint32_t wm_rank(uint32_t position, uint32_t symbol, const WMBrickHeader *wm_header, const BV_WordType *bit_vector) {
    // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::rank()

    size_t interval_start = 0;
    uint64_t bit_mask = 1ULL << (WM_LEVELS - 1);
    for (size_t level = 0; level < WM_LEVELS && position > 0; ++level) {
        size_t const ones_before_interval = _fr_rank1(interval_start, bit_vector, wm_header->fr);
        size_t const ones_before_position =
            _fr_rank1(interval_start + position, bit_vector, wm_header->fr) - ones_before_interval;
        size_t const ones_in_interval =
            ones_before_interval - wm_header->ones_before_level[level];
        if (symbol & bit_mask) {
            position = ones_before_position;
            interval_start = ((level + 1) * wm_header->text_size) + wm_header->zeros_on_level[level] +
                             ones_in_interval;
        } else {
            position = position - ones_before_position;
            interval_start =
                ((level + 1) * wm_header->text_size) +
                (interval_start - (level * wm_header->text_size) - ones_in_interval);
        }
        bit_mask >>= 1;
    }
    return position;
}

// ===============================================================================================================//
//                                          HUFFMAN WAVELET MATRIX                                                //
// ===============================================================================================================//

inline uint32_t wmh_getLevelStart(uint32_t level, const glm::uvec4 &level_starts_1_to_4) {
    level--; // Force overflow for level 0 (uint). Will be optimized away for any getLevelStart(level+1) call.
    // For L0, 0 is correct. For L5 (complete bit vector size), may return any value as it is never used.
    return level < 4 ? level_starts_1_to_4[level] : 0u;
}

/// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
/// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
/// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
/// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
/// The first 4 bit element start4bit must be the first position in a 32bit memory location.
/// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
/// v[0] = 0 being the start operation count of the first LOD.
/// @return the new end4bit endpoint measured in number of 4 bit elements
uint32_t packWaveletMatrixHuffman(uint32_t *v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count) {
    // Construct a temporary WaveletMatrix Object from the input stream
    HuffmanWaveletMatrix wm(v, start4bit, end4bit);

    // --- brick header extension ---
    uint32_t out_i = start4bit / 8u; // count in 32 bit instead of 4 bit elements
    // (overwrite header start indices, LOD 0) ?
    // uint32_t bit.vec. size | 5x uint32 ones before level | 5x uint32 zeros in level | 5x bit vector level starts
    v[out_i++] = wm.getBitVector()->size();
    for (int _i = 0; _i < 5; _i++)
        v[out_i++] = wm.getOnesBeforeLevel()[_i];
    for (int _i = 0; _i < 4; _i++)
        v[out_i++] = wm.getLevelStarts()[_i + 1]; // first start is always 0

    // keep track of end4bit
    end4bit = out_i * 8;
    // update header so that v[0, .. LOD_COUNT) contain the operations count at which each LOD starts. This is a
    // constant offset on each LOD start so that v[0] = 0.
    uint32_t base_header_size = v[0];
    for (int _i = 0; _i < lod_count; _i++)
        v[_i] -= base_header_size;

    // ---- 64 bit encoding begins ----
    static_assert(sizeof(BV_WordType) == sizeof(BV_L12Type));
    BV_WordType *v64 = reinterpret_cast<uint64_t *>(&v[out_i]);
    out_i = 0u;

    // FlatRank
    const BV_L12Type *fr = wm.getFlatRank()->getRawData();
    assert(wm.getFlatRank()->getRawDataSize() == getFlatRankEntries(v[start4bit / 8u]) && "Flat rank size does not match expected size.");
    for (uint32_t _i = 0u; _i < wm.getFlatRank()->getRawDataSize(); _i++) {
        v64[out_i++] = fr[_i];
    }
    // Bit Vector
    const BV_WordType *bv = wm.getBitVector()->getRawDataConst();
    for (uint32_t _i = 0u; _i < wm.getBitVector()->getRawDataSize(); _i++) {
        v64[out_i++] = bv[_i];
    }

    // return the new end4bit
    return end4bit + out_i * 16;
}

uint32_t getEncodingIndexWithStopBits(uint32_t &inv_lod, uint32_t &inv_lod_op_i, const uint32_t *inv_lod_starts,
                                      const FlatRank_BitVector_ptrs &stop_bits) {
    uint32_t offset = 0u;
    uint32_t covered_nodes_shift = 3 * inv_lod;

    for (int l = 0; l < inv_lod; l++) {
        // encoding index of the parent node within its inverse LOD.
        // each parent node covers 2³ nodes in the next level, (2³)³ nodes in the next level afterwards etc.
        const uint32_t parent_op_i = inv_lod_op_i >> covered_nodes_shift;

        // if the parent sets a stop bit: set inv_lod and inv_lod_op_i so that they update
        if (_bv_access(inv_lod_starts[l] + parent_op_i - offset, stop_bits.bv)) {
            inv_lod = l;
            inv_lod_op_i = parent_op_i;
            break;
        }

        // TODO: can the second _fr_rank1 be computed iteratively or cancelled out?
        // add offset introduced by the nodes in this LOD
        offset += _fr_rank1(inv_lod_starts[l] + parent_op_i - offset, stop_bits.bv, stop_bits.fr) - _fr_rank1(inv_lod_starts[l], stop_bits.bv, stop_bits.fr);

        // in the next finer LOD, the (grand-)parent grid node covers only 1/8th of the nodes in inv_lod compared
        // to the parent from the coarser level
        covered_nodes_shift -= 3u;
        offset *= 8u;
    }

    assert(offset <= inv_lod_op_i && "stop bit offset too large");
    return inv_lod_starts[inv_lod] + inv_lod_op_i - offset;
}

uint32_t wm_huffman_access(uint32_t position, const WMHBrickHeader *wm_header, const BV_WordType *bit_vector) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::access()

    // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
    // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
    for (uint32_t level = 0; level < HWM_LEVELS; level++) {
        //            Logger(Warn) << "||| level "<< level <<" ||| bv_access(" << position << ")=" << _bv_access(position, bit_vector)
        //            << " fr_rank1=" << _fr_rank1(position, bit_vector, wm_header->fr) << ", ones_before_level="
        //            << wm_header->ones_before_level[level];

        assert(position < wm_header->bit_vector_size && "reading bit vector index out of bounds.");
        if (_bv_access(position, bit_vector)) {
            assert(position != 0u || level == 4u && "first operation in stream must be 4u (PALETTE_ADV).");
            return level;
        } else {
            // TODO: we should not use the inverted CHC but the normal CHC, interpret 1 as left and 0 as right
            //  in the wavelet matrix to optimize the rank0 / rank1 queries
            assert(position >= wmh_getLevelStart(level, wm_header->level_starts_1_to_4) && "position outside of level");
            assert(_fr_rank1(position, bit_vector, wm_header->fr) >= wm_header->ones_before_level[level] && "rank1 must yield at least as many rank1 entries as there are before the level");
            uint32_t const ones_before = _fr_rank1(position, bit_vector, wm_header->fr) - wm_header->ones_before_level[level];
            uint32_t const zeros_before = (position - wmh_getLevelStart(level, wm_header->level_starts_1_to_4)) - ones_before;
            position = wmh_getLevelStart(level + 1, wm_header->level_starts_1_to_4) + zeros_before;
        }
    }
    return HWM_LEVELS;
}

uint32_t wm_huffman_rank(uint32_t position, uint32_t symbol, const WMHBrickHeader *wm_header, const BV_WordType *bit_vector) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::rank()

    const HuffmanCode chc = HuffmanWaveletMatrix::SYMBOL2CHC[symbol];
    uint32_t bit_mask = 1ULL << (HuffmanCode::CHC_BIT_SIZE - 1);
    for (uint32_t level = 0; level < chc.length && position > 0; ++level) {
        // due to the assumptions for the canonical Huffman codes used in the wavelet matrix:
        // a) ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
        //    (there is not branch for 1 child nodes as no 1 child nodes exist)
        // b) each interval_start == level_starts(level)
        // c) each ones_before_interval == wm_header.ones_before_level[level]

        const uint32_t interval_start = wmh_getLevelStart(level, wm_header->level_starts_1_to_4);
        const uint32_t ones_before_position = _fr_rank1(interval_start + position, bit_vector, wm_header->fr) - wm_header->ones_before_level[level];

        if (chc.bit_code & bit_mask)
            return ones_before_position;

        position = position - ones_before_position;
        bit_mask >>= 1;
    }
    return position;
}

} // namespace volcanite