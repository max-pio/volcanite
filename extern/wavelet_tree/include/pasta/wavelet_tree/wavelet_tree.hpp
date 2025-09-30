/*******************************************************************************
 * pasta/wavelet_tree/wavelet_tree.hpp
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

#include <concepts>
#include <iterator>
#include <vector>

#include <pasta/bit_vector/bit_vector.hpp>
#include <pasta/bit_vector/support/flat_rank_select.hpp>
#include <pasta/utils/concepts/alphabet.hpp>
#include <pasta/utils/histogram.hpp>

#include "pasta/wavelet_tree/prefix_counting.hpp"
#include "pasta/wavelet_tree/wavelet_types.hpp"

namespace pasta {

//! \addtogroup pasta_wavelet_trees
//! \{

/*!
 * \brief Base class for wavelet trees and matrices providing access, rank,
 * and select operations.
 *
 * The base class for either a level-wise wavelet tree or a wavelet matrix.
 *
 * This class should not be constructed manually, instead the factory
 * functions \ref make_wt() and \ref make_wm() should be used.
 *
 * \tparam BitVectorType Type of bit vector used to represent the levels.
 * \tparam Symbol Type of characters in the text.
 * \tparam WaveletType Type of the \c WaveletBase. Either
 * \c WaveletTypes::Tree or WaveletTypes::Matrix.
 */
template <typename BitVectorType, typename Symbol, WaveletTypes WaveletType>
class WaveletBase {

  //! Is the wavelet base a wavelet tree.
  static constexpr bool IsTree = WaveletType == WaveletTypes::TREE;
  //! Is the wavelet base a wavelet matrix.
  static constexpr bool IsMatrix = !IsTree;
  //! Maximal number of levels in the wavelet tree/matrix w.r.t. the \c Symbol
  //! type.
  static constexpr size_t MaxLevels =
      std::bit_width(std::numeric_limits<Symbol>::max());

  //! Number of levels of the wavelet tree.
  size_t levels_;
  //! Number of bits per level, i.e., the text size.
  size_t text_size_;

  //! Bit vector containing all levels' bit vectors.
  BitVectorType bv_;
  //! Rank and select support, which is needed for navigation.
  FlatRankSelect<> rss_;

  //! Number of zeros in each level of the wavelet matrix.
  //! Not used when this is a wavelet tree.
  std::array<size_t, IsMatrix ? MaxLevels : 0> zeros_on_level_;
  //! Number of ones before each level of the wavelet matrix.
  //! Not used when this is a wavelet tree.
  std::array<size_t, IsMatrix ? MaxLevels : 0> ones_before_;

  ///! Memory needed for backtracking in select queries
  mutable std::vector<size_t> backtrack_interval_starts_;
  ///! Memory needed for backtracking in select queries.
  mutable std::vector<size_t> backtrack_interval_ranks_;

public:
  /*!
   * \brief Constructor. Constructs the wavelet base if a compressed bit
   * vector is used.
   *
   * \tparam InputIterator Iterator type of the text container.
   * \param begin Iterator to the beginning of the text.
   * \param end Iterator marking the end of the text.
   * \param alphabet_size size of the alphabet of the input text.
   */
  template <std::forward_iterator InputIterator>
  requires SmallAlphabet<std::iter_value_t<InputIterator>>
  WaveletBase(InputIterator begin, InputIterator end,
              size_t const alphabet_size)
  noexcept
      : levels_(std::bit_width(alphabet_size - 1)),
        text_size_(std::distance(begin, end)),
        backtrack_interval_starts_(levels_ + 1, 0),
        backtrack_interval_ranks_(levels_ + 1, 0) {

    BitVector tmp_bv(text_size_ * levels_, 0);
    prefix_counting<WaveletType>(begin, end, levels_, tmp_bv);
    bv_ = std::move(BitVectorType(std::move(tmp_bv)));
    init_rank_select();
  }

  /*!
   * \brief Constructor. Constructs the wavelet base if an uncompressed bit
   * vector is used.
   *
   * \tparam InputIterator Iterator type of the text container.
   * \param begin Iterator to the beginning of the text.
   * \param end Iterator marking the end of the text.
   * \param alphabet_size size of the alphabet of the input text.
   */
  template <std::forward_iterator InputIterator>
  requires SmallAlphabet<std::iter_value_t<InputIterator>> &&
      std::same_as<BitVectorType, BitVector>
      WaveletBase(InputIterator begin, InputIterator end,
                  size_t const alphabet_size)
  noexcept
      : levels_(std::bit_width(alphabet_size - 1)),
        text_size_(std::distance(begin, end)), bv_(text_size_ * levels_, 0),
        backtrack_interval_starts_(levels_ + 1, 0),
        backtrack_interval_ranks_(levels_ + 1, 0) {

    prefix_counting<WaveletType>(begin, end, levels_, bv_);
    init_rank_select();
  }

  /*!
   * \brief Access operator to access characters of the text using the wavelet
   * tree/matrix.
   *
   * \param position Position of the character that should be retrieved.
   * \return Character at position \c position.
   */
  [[nodiscard("Wavelet tree accessed but result not used")]] Symbol
  operator[](size_t position) const noexcept {
    Symbol result = 0ULL;
    if constexpr (IsTree) {
      size_t interval_start = 0;
      size_t interval_size = text_size_;
      for (size_t level = 0; level < levels_;
           ++level, interval_start += text_size_) {
        result <<= 1;
        // we compute the number of ones instead of zeros as described for
        // example in "The WM: An efficient WT for large alphabets", because
        // rank1 requires one subtraction less than rank0 (as implemented
        // here).
        size_t const ones_before_interval = rss_.rank1(interval_start);
        size_t const ones_before_position =
            rss_.rank1(interval_start + position) - ones_before_interval;
        size_t const ones_in_interval =
            rss_.rank1(interval_start + interval_size) - ones_before_interval;
        if (bv_[interval_start + position]) {
          result |= 1ULL;
          interval_start += (interval_size - ones_in_interval);
          interval_size = ones_in_interval;
          position = ones_before_position;
        } else {
          interval_size -= ones_in_interval;
          position -= ones_before_position;
        }
      }
    } else {
      bool bit = bv_[position];
      for (size_t level = 0; level < levels_; ++level) {
        result <<= 1;
        size_t const ones_before = rss_.rank1(position) - ones_before_[level];
        if (bit) {
          result |= 1ULL;
          position =
              (level + 1) * text_size_ + zeros_on_level_[level] + ones_before;
        } else {
          size_t const zeros_before =
              (position - (level * text_size_)) - ones_before;
          position = (level + 1) * text_size_ + zeros_before;
        }
        bit = bv_[position];
      }
    }
    return result;
  }

  /*!
   * \brief Computes the number of occurrences of a symbol before the given
   * position \c p, i.e., in the interval [0..p).
   *
   * \param position The position up to (not included) the occurrences are
   * counted.
   * \param symbol The symbol the occurrences are counted of.
   * \return The number of occurrences of \c symbol in the interval
   * [0..\c position).
   */
  [[nodiscard("Wavelet tree rank computed but result not used")]] size_t
  rank(size_t position, Symbol const symbol) const noexcept {
    size_t interval_start = 0;
    uint64_t bit_mask = 1ULL << (levels_ - 1);
    if constexpr (IsTree) {
      size_t interval_size = text_size_;
      for (size_t level = 0; level < levels_ && position > 0;
           ++level, interval_start += text_size_) {
        // we compute the number of ones instead of zeros as described for
        // example in "The WM: An efficient WT for large alphabets", because
        // rank1 requires one subtraction less than rank0 (as implemented
        // here).
        size_t const ones_before_interval = rss_.rank1(interval_start);
        size_t const ones_before_position =
            rss_.rank1(interval_start + position) - ones_before_interval;
        size_t const ones_in_interval =
            rss_.rank1(interval_start + interval_size) - ones_before_interval;
        if (symbol & bit_mask) {
          interval_start += (interval_size - ones_in_interval);
          interval_size = ones_in_interval;
          position = ones_before_position;
        } else {
          interval_size -= ones_in_interval;
          position -= ones_before_position;
        }
        bit_mask >>= 1;
      }
    } else {
      for (size_t level = 0; level < levels_ && position > 0; ++level) {
        size_t const ones_before_interval = rss_.rank1(interval_start);
        size_t const ones_before_position =
            rss_.rank1(interval_start + position) - ones_before_interval;
        size_t const ones_in_interval =
            ones_before_interval - ones_before_[level];
        if (symbol & bit_mask) {
          position = ones_before_position;
          interval_start = ((level + 1) * text_size_) + zeros_on_level_[level] +
                           ones_in_interval;
        } else {
          position = position - ones_before_position;
          interval_start =
              ((level + 1) * text_size_) +
              (interval_start - (level * text_size_) - ones_in_interval);
        }
        bit_mask >>= 1;
      }
    }
    return position;
  }

  /*!
   * \brief Computes the position a symbol with a specific rank, i.e., the
   * rank-th occurrence of a symbol.
   *
   * \param rank The rank of the symbol that is looked for.
   * \param symbol The symbol the position of the rank-th occurrence is looked
   * for.
   * \return Position of the \c rank-th occurrence of \c symbol.
   */
  [[nodiscard("Wavelet tree select computed but result not used")]] size_t
  select(size_t rank, Symbol const symbol) const noexcept {
    uint64_t bit_mask = 1ULL << (levels_ - 1);
    size_t interval_start = 0;
    if constexpr (IsTree) {
      size_t interval_size = text_size_;
      for (size_t level = 0; level < levels_ && interval_size > 0; ++level) {
        // we compute the number of ones instead of zeros as described for
        // example in "The WM: An efficient WT for large alphabets", because
        // rank1 requires one subtraction less than rank0 (as implemented
        // here).
        size_t const ones_before_interval = rss_.rank1(interval_start);
        size_t const ones_before_position =
            rss_.rank1(interval_start + interval_size) - ones_before_interval;
        backtrack_interval_ranks_[level] = ones_before_interval;
        if (symbol & bit_mask) {
          interval_start += (interval_size - ones_before_position);
          interval_size = ones_before_position;
        } else {
          interval_size -= ones_before_position;
        }
        interval_start += text_size_;
        backtrack_interval_starts_[level + 1] = interval_start;
        bit_mask >>= 1;
      }
      if (interval_size == 0 || interval_size < rank) {
        return text_size_;
      }
    } else {
      size_t const init_rank = rank;
      for (size_t level = 0; level < levels_; ++level) {
        size_t const ones_before_interval = rss_.rank1(interval_start);
        size_t const ones_before_position =
            rss_.rank1(interval_start + rank) - ones_before_interval;
        backtrack_interval_ranks_[level] = ones_before_interval;
        size_t const ones_in_interval =
            ones_before_interval - ones_before_[level];
        if (symbol & bit_mask) {
          rank = ones_before_position;
          interval_start = ((level + 1) * text_size_) + zeros_on_level_[level] +
                           ones_in_interval;
        } else {
          rank = rank - ones_before_position;
          interval_start =
              ((level + 1) * text_size_) +
              (interval_start - (level * text_size_) - ones_in_interval);
        }
        backtrack_interval_starts_[level + 1] = interval_start;
        bit_mask >>= 1;
      }
      rank = init_rank;
    }
    bit_mask = 1ULL;
    for (size_t level = levels_; level > 0; --level) {
      interval_start = backtrack_interval_starts_[level - 1];
      size_t const ones_before_interval = backtrack_interval_ranks_[level - 1];
      if (symbol & bit_mask) {
        rank = rss_.select1(ones_before_interval + rank) - interval_start + 1;
      } else {
        rank = rss_.select0(interval_start - ones_before_interval + rank) -
               interval_start + 1;
      }
      bit_mask <<= 1;
    }
    return rank - 1;
  }

  /*!
   * \brief Estimate for the space usage.
   * \return Number of bytes used by this data structure.
   */
  [[nodiscard("space usage computed but not used")]] size_t
  space_usage() const {
    return (backtrack_interval_starts_.size() * sizeof(size_t)) +
      (backtrack_interval_ranks_.size() * sizeof(size_t)) +
      rss_.space_usage() + bv_.space_usage();
  }

private:
  //! Initializing rank and select structure and additional arrays needed for
  //! the wavelet matrix.
  inline void init_rank_select() noexcept {
    rss_ = FlatRankSelect(bv_);
    if constexpr (IsMatrix) {
      size_t prev_zeros = 0;
      for (size_t i = 0; i < levels_; ++i) {
        size_t const total_zeros = rss_.rank0((i + 1) * text_size_);
        zeros_on_level_[i] = total_zeros - prev_zeros;
        prev_zeros = total_zeros;
        ones_before_[i] = rss_.rank1(i * text_size_);
      }
    }
  }

}; // class WaveletBase

/*!
 * \brief Factory function to construct a wavelet tree (for better template
 * deduction).
 *
 * \tparam BitVectorType Type of bit vector used in the wavelet tree.
 * \tparam InputIterator Iterator type of the iterator used for text access.
 * \param begin Iterator to the beginning of the text.
 * \param end Iterator marking the end of the text.
 * \param alphabet_size Size of the alphabet of the input text.
 * \return Wavelet tree for the given input text.
 */
template <typename BitVectorType, std::forward_iterator InputIterator>
[[nodiscard("Wavelet tree created and not used")]] WaveletBase<
    BitVectorType, std::iter_value_t<InputIterator>, WaveletTypes::TREE>
make_wt(InputIterator begin, InputIterator end, size_t const alphabet_size) {
  return WaveletBase<BitVectorType, std::iter_value_t<InputIterator>,
                     WaveletTypes::TREE>(begin, end, alphabet_size);
}

/*!
 * \brief Factory function to construct a wavelet matrix (for better template
 * deduction).
 *
 * \tparam BitVectorType Type of bit vector used in the wavelet matrix.
 * \tparam InputIterator Iterator type of the iterator used for text access.
 * \param begin Iterator to the beginning of the text.
 * \param end Iterator marking the end of the text.
 * \param alphabet_size Size of the alphabet of the input text.
 * \return Wavelet matrix for the given input text.
 */
template <typename BitVectorType, std::forward_iterator InputIterator>
[[nodiscard("Wavelet matrix created and not used")]] WaveletBase<
    BitVectorType, std::iter_value_t<InputIterator>, WaveletTypes::MATRIX>
make_wm(InputIterator begin, InputIterator end, size_t const alphabet_size) {
  return WaveletBase<BitVectorType, std::iter_value_t<InputIterator>,
                     WaveletTypes::MATRIX>(begin, end, alphabet_size);
}

//! \}

} // namespace pasta

/******************************************************************************/
