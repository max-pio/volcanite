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
#include "volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"
#include "volcanite/compression/wavelet_tree/bit_reversal_permutation.hpp"

#include <array>

void volcanite::prefix_counting(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit,
                                BitVector &bit_vector_out) {
    static_assert(WM_ALPHABET_SIZE == (1ULL << WM_LEVELS), "WM_ALPHABET_SIZE must be 2^{WM_LEVELS}.");
    // the bit_blocks that are constructed are uint64 currently. for other word sizes, generalize the construction.
    static_assert(BV_WORD_BIT_SIZE == sizeof(uint64_t) * 8u, "Prefix counting wavelet matrix construction unable to"
                                                             " handle bit vector word size.");
    assert(bit_vector_out.size() == (end4bit - start4bit) * WM_LEVELS && "bit vector has non-fitting capacity for construction");

    using HistType = std::array<uint32_t, WM_ALPHABET_SIZE>;

    uint32_t const text_size = end4bit - start4bit;
    auto raw_bv = bit_vector_out.getRawData();

    HistType hist = {0};
    HistType borders = {0};

    // Compute histogram of symbols and first level of the wavelet matrix by scanning the text from front to back.
    // The first level contains the MSB of the symbols [MSB] {MSB(-1) ... LSB}
    uint64_t const mask = 1ULL << (WM_LEVELS - 1);
    size_t const shift_first_right = BV_WORD_BIT_SIZE - WM_LEVELS;
    auto text_it = start4bit;
    size_t raw_bv_pos = 0;
    while (text_it + BV_WORD_BIT_SIZE < end4bit) {
        uint64_t bit_block = 0ULL;
        for (size_t i = 0; i < BV_WORD_BIT_SIZE; ++i, ++text_it) {
            // bits are inserted on the left (MSB) but then shifted to the right (LSB) in each iteration so that the
            // bit that was inserted first will be the LSB of the resulting BV_WordType bit_block.
            bit_block >>= 1;
            auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it);
            assert(symbol < WM_ALPHABET_SIZE && "symbol is higher than the alphabet size");
            ++hist[symbol];
            bit_block |= (symbol & mask) << shift_first_right;
        }
        raw_bv[raw_bv_pos++] = bit_block;
    }
    // Scan the remaining entries (fewer than 64 bit entries for the last word).
    uint64_t bit_block = 0ULL;
    size_t const remainder = end4bit - text_it;
    for (size_t i = 0; i < remainder; ++i) {
        auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it + i);
        ++hist[symbol];
        bit_block >>= 1;
        bit_block |= (symbol & mask) << shift_first_right;
    }
    if (remainder > 0) [[likely]] {
        bit_block >>= (BV_WORD_BIT_SIZE - remainder);
        raw_bv[raw_bv_pos] = bit_block;
    }

    // construct the other levels bottom up
    size_t cur_alphabet_size = (1ULL << WM_LEVELS);
    for (size_t level = WM_LEVELS - 1; level > 0; --level) {
        cur_alphabet_size >>= 1;
        // Update the histograms so that they count the reduced alphabet of the first (WM_LEVELS - level) first MSB:
        // [MSB, MSB(-1), MSB(-2) .. MSB(-level)] {MSB(-level-1)... LSB}
        // where hist of the first j levels can be constructed from the histogram of the (j+1) levels by adding up the
        // two buckets with the same (WM_LEVELS - j) MSB as
        // hist_j[***] = hist_{j+1}[***0] + hist_{j+1}[***1]
        for (size_t i = 0; i < cur_alphabet_size; ++i) {
            // reuse the space in borders as temporary memory to aggregate the histograms
            borders[i] = hist[i << 1] + hist[(i << 1) + 1];
        }
        std::copy_n(borders.begin(), cur_alphabet_size, hist.begin());

        // As opposed to the wavelet tree, where borders[i] = hits[i-1] + borders[i-1]:
        // in the wavelet matrix, the intervals occur in order of the bit-reversal permutation of the text symbols.
        auto const brv = pasta::BitReversalPermutation[level];
        borders[0] = text_size * level; // brv[0] = 0
        for (size_t i = 1; i < cur_alphabet_size; ++i) {
            borders[brv[i]] = hist[brv[i - 1]] + borders[brv[i - 1]];
        }

        size_t const shift_word_for_bit = WM_LEVELS - level - 1;
        for (auto it = start4bit; it < end4bit; ++it) {
            auto const symbol_prefix = (volcanite::read4Bit(op_stream_in, 0, it) >> shift_word_for_bit);
            size_t const position = borders[symbol_prefix >> 1]++;
            raw_bv[position / BV_WORD_BIT_SIZE] |= (symbol_prefix & 1ULL) << (position % BV_WORD_BIT_SIZE);
        }
    }
}

static inline bool in_next(size_t code_length, size_t level) {
    return (code_length - 1) > level;
}

void volcanite::prefix_counting_huffman(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit,
                                        BitVector &bit_vector_out, uint32_t level_starts_out[HWM_LEVELS + 1]) {
    assert(bit_vector_out.size() >= (end4bit - start4bit) * HWM_LEVELS && "bit vector has not enough capacity for construction");

    uint32_t const text_size = end4bit - start4bit;
    auto raw_bv = bit_vector_out.getRawData();

    // Compute the first level of the wavelet matrix by scanning the text from front to back.
    // The first level contains the MSB of the canonical Huffman codes for each text symbol.
    // Each symbol's CHC is at least one symbol long which is why the bit vector of the first WM level is exactly
    // as long as the text size.
    {
        level_starts_out[0] = 0;

        uint64_t const mask = 1ULL << (HuffmanCode::CHC_BIT_SIZE - 1);
        size_t const shift_first_right = BV_WORD_BIT_SIZE - HuffmanCode::CHC_BIT_SIZE;
        auto text_it = start4bit;
        size_t raw_bv_pos = 0;
        while (text_it + BV_WORD_BIT_SIZE < end4bit) {
            uint64_t bit_block = 0ULL;
            for (size_t i = 0; i < BV_WORD_BIT_SIZE; ++i, ++text_it) {
                // bits are inserted on the left (MSB) but then shifted to the right (LSB) in each iteration so that the
                // bit that was inserted first will be the LSB of the resulting BV_WordType bit_block.
                bit_block >>= 1;

                // obtain the inverted canonical Huffman code for the symbol
                auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it);
                assert(symbol < HWM_ALPHABET_SIZE && "symbol is higher than the alphabet size");
                const HuffmanCode chc = HuffmanWaveletMatrix::SYMBOL2CHC[symbol];
                bit_block |= (chc.bit_code & mask) << shift_first_right;
            }
            raw_bv[raw_bv_pos++] = bit_block;
        }
        // Scan the remaining entries (fewer than 64 bit entries for the last word).
        uint64_t bit_block = 0ULL;
        size_t const remainder = end4bit - text_it;
        for (size_t i = 0; i < remainder; ++i) {
            bit_block >>= 1;

            auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it + i);
            assert(symbol < HWM_ALPHABET_SIZE && "symbol is higher than the alphabet size");
            const HuffmanCode chc = HuffmanWaveletMatrix::SYMBOL2CHC[symbol];
            bit_block |= (chc.bit_code & mask) << shift_first_right;
        }
        if (remainder > 0) [[likely]] {
            bit_block >>= (BV_WORD_BIT_SIZE - remainder);
            raw_bv[raw_bv_pos] = bit_block;
        }
    }

    // Construct other levels top down (note: as opposed to the bottom up approach for non-Huffman shaped WM).
    // Due to the constraints for the canonical Huffman codes (any 1 IMMEDIATELY terminates the code), we do not have
    // to keep track of border arrays or histograms.
    size_t position = text_size;
    for (size_t level = 1; level < HWM_LEVELS; level++) {

        // it is necessary to keep track of the start positions of the levels, as the levels have different lengths now.
        level_starts_out[level] = position;

        size_t const shift_word_for_bit = (HuffmanCode::CHC_BIT_SIZE - 1 - level);
        for (auto text_it = start4bit; text_it < end4bit; text_it++) {
            // obtain the canonical Huffman code for the current symbol in the text
            auto const symbol = volcanite::read4Bit(op_stream_in, 0, text_it);
            assert(symbol < HWM_ALPHABET_SIZE && "symbol is higher than the alphabet size");
            const HuffmanCode &chc = HuffmanWaveletMatrix::SYMBOL2CHC[symbol];

            // If this Huffman code is still present at the given level, output its current bit, otherwise output nothing.
            // If this bit is 1, the code MUST end here.
            if (chc.length > level) {
                raw_bv[position / BV_WORD_BIT_SIZE] |= ((chc.bit_code >> shift_word_for_bit) & 1ULL) << (position % BV_WORD_BIT_SIZE);
                position++;
            }
        }
    }

    // the final bit vector is (ideally) shorter than the theoretical maximum that was reserved
    level_starts_out[HWM_LEVELS] = position;
}
