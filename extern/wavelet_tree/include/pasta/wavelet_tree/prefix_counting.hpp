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
 ******************************************************************************/

#pragma once

#include <array>
#include <bit>
#include <iterator>
#include <numeric>

#include "pasta/wavelet_tree/bit_reversal_permutation.hpp"
#include "pasta/wavelet_tree/wavelet_types.hpp"

namespace pasta {

//! \addtogroup pasta_wavelet_trees
//! \{

/*!
 * \brief Sequential prefix counting algorithm described in
 * \cite FischerKL2018PWX, based on bottom-up construction.
 *
 * The construction computes the first level of the wavelet tree/matrix and
 * the histogram of all characters. Based on the histogram, the borders for
 * all bit prefixes of all characters on all levels, i.e., the positions of
 * all bits in the wavelet tree/matrix.
 *
 * \tparam WaveletType \c WaveletTypes::Tree or \c WaveletTypes::Matrix.
 * \param begin Iterator to the beginning of the text.
 * \param end Iterator marking the end of the text.
 * \param levels Number of levels of the wavelet tree/matrix.
 * \param bit_vector_out \c BitVector the wavelet tree/matrix is stored in
 * (output parameter).
 */
template <WaveletTypes WaveletType>
void prefix_counting(std::forward_iterator auto begin,
                     std::forward_iterator auto const end, size_t const levels,
                     BitVector &bit_vector_out) {

  using HistType = std::array<
      size_t,
      std::numeric_limits<std::iter_value_t<decltype(begin)>>::max() + 1>;

  size_t const text_size = std::distance(begin, end);

  auto raw_bv = bit_vector_out.data();

  HistType hist = {0};
  HistType borders = {0};

  uint64_t const mask = 1ULL << (levels - 1);
  size_t const shift_first_right = 64 - levels;
  auto text_it = begin;
  size_t raw_bv_pos = 0;
  while (text_it + 64 < end) {
    uint64_t bit_block = 0ULL;
    for (size_t i = 0; i < 64; ++i, ++text_it) {
      bit_block >>= 1;
      auto const symbol = *text_it;
      ++hist[symbol];
      bit_block |= (symbol & mask) << shift_first_right;
    }
    raw_bv[raw_bv_pos++] = bit_block;
  }

  uint64_t bit_block = 0ULL;
  size_t const remainder = end - text_it;
  for (size_t i = 0; i < remainder; ++i) {
    auto const symbol = *(text_it + i);
    ++hist[symbol];
    bit_block >>= 1;
    bit_block |= (symbol & mask) << shift_first_right;
  }
  if (remainder > 0) [[likely]] {
    bit_block >>= (64 - remainder);
    raw_bv[raw_bv_pos] = bit_block;
  }

  size_t cur_alphabet_size = (1ULL << levels);
  for (size_t level = levels - 1; level > 0; --level) {
    cur_alphabet_size >>= 1;
    for (size_t i = 0; i < cur_alphabet_size; ++i) {
      borders[i] = hist[i << 1] + hist[(i << 1) + 1];
    }
    std::copy_n(borders.begin(), cur_alphabet_size, hist.begin());

    if constexpr (WaveletType == WaveletTypes::TREE) {
      std::exclusive_scan(borders.begin(), borders.begin() + cur_alphabet_size,
                          borders.begin(), text_size * level);
    } else {
      auto const brv = BitReversalPermutation[level];
      borders[0] = text_size * level; // brv[0] = 0
      for (size_t i = 1; i < cur_alphabet_size; ++i) {
        borders[brv[i]] = hist[brv[i - 1]] + borders[brv[i - 1]];
      }
    }

    size_t const shift_word_for_bit = levels - level - 1;
    for (auto it = begin; it < end; ++it) {
      auto const symbol_prefix = (*it >> shift_word_for_bit);
      size_t const position = borders[symbol_prefix >> 1]++;
      raw_bv[position / 64] |= (symbol_prefix & 1ULL) << (position % 64);
    }
  }
}

//! \}

} // namespace pasta

/******************************************************************************/
