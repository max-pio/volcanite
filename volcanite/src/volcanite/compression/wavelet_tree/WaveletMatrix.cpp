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

#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"

#include "volcanite/compression/wavelet_tree/prefix_counting.hpp"

namespace volcanite {

WaveletMatrix::WaveletMatrix(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit)
    : WaveletMatrixBase(op_stream_in, start4bit, end4bit),
      m_bv(m_text_size * WM_LEVELS), m_zeros_on_level(), m_ones_before() {
    // construct the concatenated bit vector
    prefix_counting(op_stream_in, start4bit, end4bit, m_bv);
    // construct flat rank acceleration structure over the bit vector
    m_fr = new FlatRank(m_bv);
    // initialize wavelet matrix utility arrays
    size_t prev_zeros = 0;
    for (size_t i = 0; i < WM_LEVELS; ++i) {
        // rank0(N) for N=text_size is undefined, query rank0(N-1) + access(N-1) instead
        size_t const total_zeros = m_fr->rank0((i + 1) * m_text_size);
        m_zeros_on_level[i] = total_zeros - prev_zeros;
        prev_zeros = total_zeros;
        m_ones_before[i] = m_fr->rank1(i * m_text_size);
    }
}

uint32_t WaveletMatrix::access(uint32_t position) const {
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

uint32_t WaveletMatrix::rank(uint32_t position, uint32_t symbol) const {
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

} // namespace volcanite
