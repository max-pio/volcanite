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
//
// This class is based on code from the pasta-toolkit Wavelet Tree implementation by Florian Kurpicz which is licensed
// under the GPLv3 license. https://github.com/pasta-toolbox/wavelet_tree

#pragma once


#include <cassert>
#include <cstring>
#include "vvv/util/Logger.hpp"
#include "vvv/util/util.hpp"

#include "BitVector.hpp"
#include "volcanite/compression/pack_nibble.hpp"
#include "bit_reversal_permutation.hpp"

using namespace vvv;

namespace volcanite {

constexpr static uint32_t WM_LEVELS = 4u;
constexpr static uint32_t WM_ALPHABET_SIZE = 16u;

/// \brief Sequential prefix counting algorithm described in
/// \cite FischerKL2018PWX, based on bottom-up construction.
///
/// Adapted from the GPLv3 licensed prefix_counting method from the pasta toolbox Wavelet Tree implementation:
/// https://github.com/pasta-toolbox/wavelet_tree/blob/main/include/pasta/wavelet_tree/prefix_counting.hpp
///
/// The construction computes the first level of the wavelet tree/matrix and
/// the histogram of all characters. Based on the histogram, the borders for
/// all bit prefixes of all characters on all levels, i.e., the positions of
/// all bits in the wavelet tree/matrix.
///
/// \param op_stream_in pointer to a memory region containing 4 bit operations
/// \param start4bit the first index in 4 bit elements of the stream
/// \param end4bit the end index (exclusive) in 4 bit elements of the stream
/// \param bit_vector_out \c BitVector the wavelet matrix is stored in
/// (output parameter).
void prefix_counting(uint32_t* op_stream_in, uint32_t start4bit, uint32_t end4bit, BitVector& bit_vector_out) {
    using HistType = std::array<uint32_t, WM_ALPHABET_SIZE>;

    uint32_t const text_size = end4bit - start4bit;

    auto raw_bv = bit_vector_out.getRawData();

    HistType hist = {0};
    HistType borders = {0};

    uint64_t const mask = 1ULL << (WM_LEVELS - 1);
    size_t const shift_first_right = 64 - WM_LEVELS;
    auto text_it = start4bit;
    size_t raw_bv_pos = 0;
    while (text_it + 64 < end4bit) {
        uint64_t bit_block = 0ULL;
        for (size_t i = 0; i < 64; ++i, ++text_it) {
            bit_block >>= 1;
            auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it);
            ++hist[symbol];
            bit_block |= (symbol & mask) << shift_first_right;
        }
        raw_bv[raw_bv_pos++] = bit_block;
    }

    uint64_t bit_block = 0ULL;
    size_t const remainder = end4bit - text_it;
    for (size_t i = 0; i < remainder; ++i) {
        auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it + i);
        ++hist[symbol];
        bit_block >>= 1;
        bit_block |= (symbol & mask) << shift_first_right;
    }
    if (remainder > 0) [[likely]] {
        bit_block >>= (64 - remainder);
        raw_bv[raw_bv_pos] = bit_block;
    }

    size_t cur_alphabet_size = (1ULL << WM_LEVELS);
    for (size_t level = WM_LEVELS - 1; level > 0; --level) {
        cur_alphabet_size >>= 1;
        for (size_t i = 0; i < cur_alphabet_size; ++i) {
            borders[i] = hist[i << 1] + hist[(i << 1) + 1];
        }
        std::copy_n(borders.begin(), cur_alphabet_size, hist.begin());


        auto const brv = pasta::BitReversalPermutation[level];
        borders[0] = text_size * level; // brv[0] = 0
        for (size_t i = 1; i < cur_alphabet_size; ++i) {
            borders[brv[i]] = hist[brv[i - 1]] + borders[brv[i - 1]];
        }

        size_t const shift_word_for_bit = WM_LEVELS - level - 1;
        for (auto it = start4bit; it < end4bit; ++it) {
            auto const symbol_prefix = (volcanite::read4Bit(op_stream_in, 0, it) >> shift_word_for_bit);
            size_t const position = borders[symbol_prefix >> 1]++;
            raw_bv[position / 64] |= (symbol_prefix & 1ULL) << (position % 64);
        }
    }
}

class WaveletMatrix {

    private:
        uint32_t m_text_size;
        BitVector m_bv;                          ///< Wavelet matrix bit vectors of all 4 levels concatenated.
        FlatRank* m_fr;                          ///< Flat rank L12-block acceleration structure for rank operations.
        uint32_t m_zeros_on_level[WM_LEVELS];    ///< Number of zeros in each level of the wavelet matrix.
        uint32_t m_ones_before[WM_LEVELS];       ///< Number of ones before each level of the wavelet matrix.

    public:
        WaveletMatrix(uint32_t* op_stream_in, uint32_t start4bit, uint32_t end4bit)
            : m_text_size(end4bit - start4bit),
              m_bv(m_text_size * WM_LEVELS, 0u) {

            // construct the concatenated bit vector
            prefix_counting(op_stream_in, start4bit, end4bit, m_bv);
            // construct flat rank acceleration structure over the bit vector
            m_fr = new FlatRank(m_bv);
            // initialize wavelet matrix utility arrays
            size_t prev_zeros = 0;
            for (size_t i = 0; i < WM_LEVELS; ++i) {
                // rank0(N) for N=text_size is undefined, query rank0(N-1) + access(N-1) instead
                size_t const total_zeros = m_fr->rank0( (i + 1) * m_text_size);
                m_zeros_on_level[i] = total_zeros - prev_zeros;
                prev_zeros = total_zeros;
                m_ones_before[i] = m_fr->rank1(i * m_text_size);
            }
        }

        [[nodiscard]] uint32_t access(uint32_t position) const {
            assert(position < m_text_size && "accessing symbol position out of bounds of wavelet matrix");
            uint32_t result = 0u;
            bool bit = m_bv.access(position);
            for (size_t level = 0; level < WM_LEVELS; ++level) {
                result <<= 1;
                size_t const ones_before = m_fr->rank1(position) - m_ones_before[level];
                if (bit) {
                    result |= 1ULL;
                    position =
                            (level + 1) * m_text_size + m_zeros_on_level[level] + ones_before;
                } else {
                    size_t const zeros_before =
                            (position - (level * m_text_size)) - ones_before;
                    position = (level + 1) * m_text_size + zeros_before;
                }
                if (level < WM_LEVELS - 1u)
                    bit = m_bv.access(position);
            }
            return result;
        }

        [[nodiscard]] uint32_t rank(uint32_t position, uint32_t symbol) const {
            size_t interval_start = 0;
            uint64_t bit_mask = 1ULL << (WM_LEVELS - 1);
            for (size_t level = 0; level < WM_LEVELS && position > 0; ++level) {
                size_t const ones_before_interval = m_fr->rank1(interval_start);
                size_t const ones_before_position =
                        m_fr->rank1(interval_start + position) - ones_before_interval;
                size_t const ones_in_interval =
                        ones_before_interval - m_ones_before[level];
                if (symbol & bit_mask) {
                    position = ones_before_position;
                    interval_start = ((level + 1) * m_text_size) + m_zeros_on_level[level] +
                                     ones_in_interval;
                } else {
                    position = position - ones_before_position;
                    interval_start =
                            ((level + 1) * m_text_size) +
                            (interval_start - (level * m_text_size) - ones_in_interval);
                }
                bit_mask >>= 1;
            }
            return position;
        }

        [[nodiscard]] uint32_t getTextSize() const { return m_text_size; }
        const BitVector* getBitVector() { return &m_bv; }
        const FlatRank* getFlatRank() { return m_fr; }
        [[nodiscard]] glm::uvec4 getZerosInLevel() const { return {m_zeros_on_level[0], m_zeros_on_level[1],
                                                                   m_zeros_on_level[2], m_zeros_on_level[3]}; }
        [[nodiscard]] glm::uvec4 getOnesBeforeLevel() const { return {m_ones_before[0], m_ones_before[1],
                                                                      m_ones_before[2], m_ones_before[3]}; }

        [[nodiscard]] size_t getByteSize() const {
            size_t bytes = 9 * sizeof(uint32_t)                            // ones_before, zeros_on_level, text_size
                           + m_bv.getRawDataSize() * sizeof(BV_WordType)   // bit vector(s) for all levels
                           + m_fr->getRawDataSize() * sizeof(BV_L12Type) + 12;  // FlatRank incl. size and data pointer
            return bytes;
        }
    };

} // namespace volcanite
