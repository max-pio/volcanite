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

#include "volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp"

#include "volcanite/compression/wavelet_tree/prefix_counting.hpp"

namespace volcanite {

HuffmanWaveletMatrix::HuffmanWaveletMatrix(const uint32_t *op_stream_in, const uint32_t start4bit, const uint32_t end4bit)
    : WaveletMatrixBase(op_stream_in, start4bit, end4bit),
      m_bv(m_text_size * HWM_LEVELS) {

    // construct the concatenated bit vector
    prefix_counting_huffman(op_stream_in, start4bit, end4bit, m_bv, m_level_starts);
    // resize bit vector to its actual length - which is shorter than text_size * HWM_LEVELS
    m_bv.resize(m_level_starts[HWM_LEVELS]);

    // construct flat rank acceleration structure over the bit vector
    m_fr = new FlatRank(m_bv);

    // initialize wavelet matrix utility arrays
    // size_t prev_zeros = 0;
    for (size_t i = 0; i < HWM_LEVELS; ++i) {
        // rank0(N) for N=text_size is undefined, query rank0(N-1) + access(N-1) instead
        // size_t const total_zeros = m_fr->rank0(m_level_starts[i+1]);
        // m_zeros_on_level[i] = total_zeros - prev_zeros;
        // prev_zeros = total_zeros;
        m_ones_before[i] = m_fr->rank1(m_level_starts[i]);
    }
}

uint32_t HuffmanWaveletMatrix::access(uint32_t position) const {
    assert(position < m_text_size && "accessing symbol position out of bounds of wavelet matrix");
    // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
    // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
    for (size_t level = 0; level < HWM_LEVELS; level++) {
        if (m_bv.access(position)) {
            return level;
        } else {
            // TODO: we should not use the inverted CHC but the normal CHC, interpret 1 as left and 0 as right in the wavelet matrix to optimize the rank0 / rank1 queries
            size_t const ones_before = m_fr->rank1(position) - m_ones_before[level];
            size_t const zeros_before = (position - m_level_starts[level]) - ones_before;
            position = m_level_starts[level + 1] + zeros_before;
        }
    }
    return 5u;
}

uint32_t HuffmanWaveletMatrix::rank(uint32_t position, const uint32_t symbol) const {
    size_t interval_start = 0;
    const HuffmanCode chc = SYMBOL2CHC[symbol];
    uint64_t bit_mask = 1ULL << (HuffmanCode::CHC_BIT_SIZE - 1);
    for (size_t level = 0; level < chc.length && position > 0; ++level) {
        size_t const ones_before_interval = m_fr->rank1(interval_start);
        size_t const ones_before_position = m_fr->rank1(interval_start + position) - ones_before_interval;
        // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
        // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
        if (chc.bit_code & bit_mask) {
            return ones_before_position;
        } else {
            position = position - ones_before_position;
            size_t const ones_in_interval = ones_before_interval - m_ones_before[level];
            interval_start = m_level_starts[level + 1] + (interval_start - m_level_starts[level] - ones_in_interval);
        }
        bit_mask >>= 1;
    }
    return position;
}

} // namespace volcanite
