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

#include <cstdint>
#include <vector>
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"

namespace volcanite {

    /// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
    /// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
    /// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
    /// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
    /// The first 4 bit element start4bit must be the first position in a 32bit memory location.
    /// @return the new end4bit endpoint measured in number of 4 bit elements
    uint32_t packWaveletMatrix(uint32_t* v, std::size_t start4bit, std::size_t end4bit) {
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
        // update header so that v[0] points to the start of the flat rank
        v[0] = end4bit;

        // ---- 64 bit encoding begins ----
        uint64_t* v64 = reinterpret_cast<uint64_t*>(&v[out_i]);
        out_i = 0u;

        // FlatRank
        const uint64_t* fr = wm.getFlatRank()->getRawData();
        for (uint32_t _i = 0u; _i < wm.getFlatRank()->getRawDataSize(); _i++) {
            v64[out_i++] = fr[_i];
        }
        // Bit Vector
        const uint64_t* bv = wm.getBitVector()->getRawDataConst();
        for (uint32_t _i = 0u; _i < wm.getBitVector()->getRawDataSize(); _i++) {
            v64[out_i++] = bv[_i];
        }

        // return the new end4bit
        return end4bit + out_i * 16;
    }

    inline uint32_t _bv_access(uint32_t index, const uint64_t* bv) {
        return bitfieldExtract(bv[index / BV_WORD_BIT_SIZE], static_cast<int>(index % BV_WORD_BIT_SIZE), 1);
    }

    // HELPER FUNCTIONS FOR BIT VECTOR ACCESS AND RANK =================================================================

    uint32_t _fr_rank1(uint32_t index, const uint64_t* bv, const uint64_t* fr) {
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
        return rank1_res + rank1Word(bv[offset], index % BV_WORD_BIT_SIZE);
    }

    inline uint32_t _fr_rank0(uint32_t index, const uint64_t* bv, const uint64_t* fr) {
        return index - _fr_rank1(index, bv, fr);
    }


    // WAVELET MATRIX ACCESS AND RANK ==================================================================================

    uint32_t wm_access(uint32_t position, uint32_t text_size,
                       const uint64_t* bit_vector, const uint64_t* flat_rank,
                       const uint32_t ones_before_level[4], const uint32_t zeros_on_level[4]) {
        // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::access()

        assert(position < text_size && "accessing symbol position out of bounds of wavelet matrix");
        uint32_t result = 0u;
        bool bit = _bv_access(position, bit_vector);
        for (size_t level = 0; level < WM_LEVELS; ++level) {
            result <<= 1;
            size_t const ones_before = _fr_rank1(position, bit_vector, flat_rank) - ones_before_level[level];
            if (bit) {
                result |= 1ULL;
                position =
                        (level + 1) * text_size + zeros_on_level[level] + ones_before;
            } else {
                size_t const zeros_before =
                        (position - (level * text_size)) - ones_before;
                position = (level + 1) * text_size + zeros_before;
            }
            if (level < WM_LEVELS - 1u)
                bit = _bv_access(position, bit_vector);
        }
        return result;
    }

    uint32_t wm_rank(uint32_t position, uint32_t symbol, int32_t text_size,
                     const uint64_t* bit_vector, const uint64_t* flat_rank,
                     const uint32_t ones_before_level[4], const uint32_t zeros_on_level[4]) {
        // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::rank()
        assert(position <= text_size && "rank for symbol position out of bounds of wavelet matrix");

        size_t interval_start = 0;
        uint64_t bit_mask = 1ULL << (WM_LEVELS - 1);
        for (size_t level = 0; level < WM_LEVELS && position > 0; ++level) {
            size_t const ones_before_interval = _fr_rank1(interval_start, bit_vector, flat_rank);
            size_t const ones_before_position =
                    _fr_rank1(interval_start + position, bit_vector, flat_rank) - ones_before_interval;
            size_t const ones_in_interval =
                    ones_before_interval - ones_before_level[level];
            if (symbol & bit_mask) {
                position = ones_before_position;
                interval_start = ((level + 1) * text_size) + zeros_on_level[level] +
                                 ones_in_interval;
            } else {
                position = position - ones_before_position;
                interval_start =
                        ((level + 1) * text_size) +
                        (interval_start - (level * text_size) - ones_in_interval);
            }
            bit_mask >>= 1;
        }
        return position;
    }

}