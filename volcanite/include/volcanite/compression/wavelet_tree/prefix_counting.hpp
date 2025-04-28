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

#pragma once

#include "HuffmanWaveletMatrix.hpp"

namespace volcanite {

class BitVector;

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
void prefix_counting(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit, BitVector &bit_vector_out);

/// \brief Sequential prefix counting algorithm described in
/// \cite FischerKL2018PWX, based on bottom-up construction.
///
/// Adapted from the GPLv3 licensed prefix_counting method from the pasta toolbox Wavelet Tree implementation:
/// https://github.com/pasta-toolbox/wavelet_tree/blob/main/include/pasta/wavelet_tree/prefix_counting.hpp
///
/// This prefix counting pass creates a Huffman shaped wavelet matrix bit vector. It uses the inverted canonical
/// Huffman codes from HuffmanWaveletMatrix::SYMBOL2CHC which must be assigned by a Rice coding with M=1.
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
/// \param level_starts_out the indices in the concatenated WM bit vector at which each respective WM level starts
/// (output parameter).
void prefix_counting_huffman(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit,
                             BitVector &bit_vector_out, uint32_t *level_starts_out);

} // namespace volcanite
