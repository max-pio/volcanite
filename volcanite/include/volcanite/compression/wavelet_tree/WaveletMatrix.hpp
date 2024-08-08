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

            prefix_counting(op_stream_in, start4bit, end4bit, m_bv);
        }

        uint32_t access(uint32_t* v, uint32_t start, uint32_t entry_id) const;
        uint32_t rank(uint32_t* v, uint32_t start, uint32_t entry_id) const;

    };

} // namespace volcanite
