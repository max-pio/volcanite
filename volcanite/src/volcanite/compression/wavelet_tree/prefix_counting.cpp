/*******************************************************************************
 * pasta/wavelet_tree/prefix_counting.hpp
 *
 * Copyright (C) 2021 Florian Kurpicz <florian@kurpicz.org>
 *
 * pasta::wavelet_tree is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pasta::wavelet_tree is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pasta::wavelet_tree.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ADAPTED FOR VOLCANITE BY MAX PIOCHOWIAK (2024)
 ******************************************************************************/

#include "volcanite/compression/wavelet_tree/prefix_counting.hpp"

#include "volcanite/compression/pack_nibble.hpp"
#include "volcanite/compression/wavelet_tree/BitVector.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"
#include "volcanite/compression/wavelet_tree/bit_reversal_permutation.hpp"


#include <array>


void volcanite::prefix_counting(uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit,
                                BitVector &bit_vector_out) {
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
