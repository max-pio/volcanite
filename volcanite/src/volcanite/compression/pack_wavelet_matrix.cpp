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

namespace volcanite {

    // HELPER FUNCTIONS FOR BIT VECTOR ACCESS AND RANK =================================================================


    inline uint32_t getFlatRankEntries(uint32_t text_size) {
        return (text_size * WM_LEVELS) / BV_L1_BIT_SIZE + 1u;
    }

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


    // WAVELET MATRIX PACKING, ACCESS AND RANK =========================================================================

    /// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
    /// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
    /// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
    /// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
    /// The first 4 bit element start4bit must be the first position in a 32bit memory location.
    /// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
    /// v[0] = 0 being the start operation count of the first LOD.
    /// @return the new end4bit endpoint measured in number of 4 bit elements
    uint32_t packWaveletMatrix(uint32_t* v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count) {
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
        BV_WordType* v64 = reinterpret_cast<uint64_t*>(&v[out_i]);
        out_i = 0u;

        // FlatRank
        const BV_L12Type* fr = wm.getFlatRank()->getRawData();
        for (uint32_t _i = 0u; _i < wm.getFlatRank()->getRawDataSize(); _i++) {
            v64[out_i++] = fr[_i];
        }
        // Bit Vector
        const BV_WordType* bv = wm.getBitVector()->getRawDataConst();
        for (uint32_t _i = 0u; _i < wm.getBitVector()->getRawDataSize(); _i++) {
            v64[out_i++] = bv[_i];
        }

        // return the new end4bit
        return end4bit + out_i * 16;
    }

    inline uint32_t _bv_access(uint32_t index, const uint64_t* bv) {
        return bitfieldExtract(bv[index / BV_WORD_BIT_SIZE], static_cast<int>(index % BV_WORD_BIT_SIZE), 1);
    }

    /// @param base_header_size the number of initial uint32 header elements that are not WM specific
    WMBrickHeader getWMBrickHeaderFromEncoding(const uint32_t* v, uint32_t base_header_size) {
        return {.text_size=v[base_header_size],
                .ones_before_level={v[base_header_size + 1], v[base_header_size + 2],
                                    v[base_header_size + 3], v[base_header_size + 4]},
                .zeros_on_level={v[base_header_size + 5], v[base_header_size + 6],
                                 v[base_header_size + 7], v[base_header_size + 8]},
                .fr = reinterpret_cast<const BV_L12Type*>(v + base_header_size + 9),
                .bv = reinterpret_cast<const BV_L12Type*>(v + base_header_size + 9
                                                          + (sizeof(BV_L12Type)/sizeof(uint32_t)) * getFlatRankEntries(v[base_header_size]))
        };
    }

    uint32_t wm_access(uint32_t position, const WMBrickHeader& wm_header) {
        // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::access()

        assert(position < text_size && "accessing symbol position out of bounds of wavelet matrix");
        uint32_t result = 0u;
        bool bit = _bv_access(position, wm_header.bv);
        for (int level = 0; level < WM_LEVELS; ++level) {
            result <<= 1;
            size_t const ones_before = _fr_rank1(position, wm_header.bv, wm_header.fr) - wm_header.ones_before_level[level];
            if (bit) {
                result |= 1ULL;
                position =
                        (level + 1) * wm_header.text_size + wm_header.zeros_on_level[level] + ones_before;
            } else {
                size_t const zeros_before =
                        (position - (level * wm_header.text_size)) - ones_before;
                position = (level + 1) * wm_header.text_size + zeros_before;
            }
            if (level < WM_LEVELS - 1u)
                bit = _bv_access(position, wm_header.bv);
        }
        return result;
    }

    uint32_t wm_rank(uint32_t position, uint32_t symbol, const WMBrickHeader& wm_header) {
        // see: volcanite/compression/wavelet_tree/WaveletMatrix.hpp WaveletMatrix::rank()
        assert(position <= text_size && "rank for symbol position out of bounds of wavelet matrix");

        size_t interval_start = 0;
        uint64_t bit_mask = 1ULL << (WM_LEVELS - 1);
        for (size_t level = 0; level < WM_LEVELS && position > 0; ++level) {
            size_t const ones_before_interval = _fr_rank1(interval_start, wm_header.bv, wm_header.fr);
            size_t const ones_before_position =
                    _fr_rank1(interval_start + position, wm_header.bv, wm_header.fr) - ones_before_interval;
            size_t const ones_in_interval =
                    ones_before_interval - wm_header.ones_before_level[level];
            if (symbol & bit_mask) {
                position = ones_before_position;
                interval_start = ((level + 1) * wm_header.text_size) + wm_header.zeros_on_level[level] +
                                 ones_in_interval;
            } else {
                position = position - ones_before_position;
                interval_start =
                        ((level + 1) * wm_header.text_size) +
                        (interval_start - (level * wm_header.text_size) - ones_in_interval);
            }
            bit_mask >>= 1;
        }
        return position;
    }
}