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
// This class is partially based on code from the pasta-toolkit Wavelet Tree implementation by Florian Kurpicz which is
// licensed under the GPLv3 license. https://github.com/pasta-toolbox/wavelet_tree

#pragma once


#include <cassert>
#include <cstring>
#include "vvv/util/Logger.hpp"
#include "vvv/util/util.hpp"

#include "BitVector.hpp"

using namespace vvv;

namespace volcanite {

/// \brief Sequential prefix counting algorithm described in
/// \cite FischerKL2018PWX, based on bottom-up construction.
///
/// Copied and adapted from the GPLv3 licensed prefix_counting method from the pasta toolbox Wavelet Tree implementation
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
//    using HistType = std::array<size_t, std::numeric_limits<std::iter_value_t<decltype(begin)>>::max() + 1>;
//
//    size_t const text_size = std::distance(begin, end);
//
//    auto raw_bv = bit_vector_out.data();
//
//    HistType hist = {0};
//    HistType borders = {0};
//
//    uint64_t const mask = 1ULL << (levels - 1);
//    size_t const shift_first_right = 64 - levels;
//    auto text_it = begin;
//    size_t raw_bv_pos = 0;
//    while (text_it + 64 < end) {
//        uint64_t bit_block = 0ULL;
//        for (size_t i = 0; i < 64; ++i, ++text_it) {
//            bit_block >>= 1;
//            auto const symbol = *text_it;
//            ++hist[symbol];
//            bit_block |= (symbol & mask) << shift_first_right;
//        }
//        raw_bv[raw_bv_pos++] = bit_block;
//    }
//
//    uint64_t bit_block = 0ULL;
//    size_t const remainder = end - text_it;
//    for (size_t i = 0; i < remainder; ++i) {
//        auto const symbol = *(text_it + i);
//        ++hist[symbol];
//        bit_block >>= 1;
//        bit_block |= (symbol & mask) << shift_first_right;
//    }
//    if (remainder > 0) [[likely]] {
//        bit_block >>= (64 - remainder);
//        raw_bv[raw_bv_pos] = bit_block;
//    }
//
//    size_t cur_alphabet_size = (1ULL << levels);
//    for (size_t level = levels - 1; level > 0; --level) {
//        cur_alphabet_size >>= 1;
//        for (size_t i = 0; i < cur_alphabet_size; ++i) {
//            borders[i] = hist[i << 1] + hist[(i << 1) + 1];
//        }
//        std::copy_n(borders.begin(), cur_alphabet_size, hist.begin());
//
//
//        auto const brv = BitReversalPermutation[level];
//        borders[0] = text_size * level; // brv[0] = 0
//        for (size_t i = 1; i < cur_alphabet_size; ++i) {
//            borders[brv[i]] = hist[brv[i - 1]] + borders[brv[i - 1]];
//        }
//
//        size_t const shift_word_for_bit = levels - level - 1;
//        for (auto it = begin; it < end; ++it) {
//            auto const symbol_prefix = (*it >> shift_word_for_bit);
//            size_t const position = borders[symbol_prefix >> 1]++;
//            raw_bv[position / 64] |= (symbol_prefix & 1ULL) << (position % 64);
//        }
//    }
}

class WaveletMatrix {

    private:
        constexpr static uint32_t LEVELS = 4u;
        constexpr static uint32_t ALPHABET_SIZE = 6u;
        uint32_t m_text_size;
        BitVector m_bv;                         ///< Wavelet matrix bit vectors of all 4 levels concatenated.
        FlatRank* m_fr;                          ///< Flat rank L12-block acceleration structure for rank operations.
        uint32_t m_zeros_in_level[LEVELS];      ///< Number of zeros in each level of the wavelet matrix.
        uint32_t m_ones_before_level[LEVELS];   ///< Number of ones before each level of the wavelet matrix.

    public:
        WaveletMatrix(uint32_t* op_stream_in, uint32_t start4bit, uint32_t end4bit)
            : m_text_size(end4bit - start4bit),
              m_bv(m_text_size * LEVELS, 0u) {

            // construct the concatenated bit vector
            prefix_counting(op_stream_in, start4bit, end4bit, m_bv);
            // construct flat rank acceleration structure
            m_fr = new FlatRank(m_bv);
        }

        uint32_t access(uint32_t* v, uint32_t start, uint32_t entry_id) const;
        uint32_t rank(uint32_t* v, uint32_t start, uint32_t entry_id) const;

        uint32_t getTextSize() { return m_text_size; }
        const BitVector* getBitVector() { return &m_bv; }
        const FlatRank* getFlatRank() { return m_fr; }
        glm::uvec4 getZerosInLevel() { return glm::uvec4(m_zeros_in_level[0], m_zeros_in_level[1],
                                                         m_zeros_in_level[2], m_zeros_in_level[3]); }
        glm::uvec4 getOnesBeforeLevel() { return glm::uvec4(m_ones_before_level[0], m_ones_before_level[1],
                                                            m_ones_before_level[2], m_ones_before_level[3]); }

    };

} // namespace volcanite
