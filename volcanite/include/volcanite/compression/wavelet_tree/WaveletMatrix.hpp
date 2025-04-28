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

#include "vvv/util/util.hpp"

#include "BitVector.hpp"
#include "WaveletMatrixBase.hpp"

using namespace vvv;

namespace volcanite {

constexpr static uint32_t WM_LEVELS = 4u;
constexpr static uint32_t WM_ALPHABET_SIZE = 1u << WM_LEVELS;

class WaveletMatrix : public WaveletMatrixBase {

  protected:
    BitVector m_bv;                       ///< Wavelet matrix bit vectors of all 4 levels concatenated.
    FlatRank *m_fr;                       ///< Flat rank L12-block acceleration structure for rank operations.
    uint32_t m_zeros_on_level[WM_LEVELS]; ///< Number of zeros in each level of the wavelet matrix.
    uint32_t m_ones_before[WM_LEVELS];    ///< Number of ones before each level of the wavelet matrix.

  public:
    WaveletMatrix(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit);
    ~WaveletMatrix() override { delete m_fr; };

    [[nodiscard]] uint32_t access(uint32_t position) const override;
    [[nodiscard]] uint32_t rank(uint32_t position, uint32_t symbol) const override;

    [[nodiscard]] const BitVector *getBitVector() const override { return &m_bv; }
    [[nodiscard]] const FlatRank *getFlatRank() const override { return m_fr; }

    [[nodiscard]] uint32_t getLevels() const override { return WM_LEVELS; };
    [[nodiscard]] const uint32_t *getZerosInLevel() const override { return &m_zeros_on_level[0]; }
    [[nodiscard]] const uint32_t *getOnesBeforeLevel() const override { return &m_ones_before[0]; }

    [[nodiscard]] size_t getByteSize() const override {
        const size_t bytes = (1 + 2 * WM_LEVELS) * sizeof(uint32_t)              // ones_before, zeros_on_level, text_size
                             + m_bv.getRawDataSize() * sizeof(BV_WordType)       // bit vector(s) for all levels
                             + m_fr->getRawDataSize() * sizeof(BV_L12Type) + 12; // FlatRank incl. size and data pointer
        return bytes;
    }
};

} // namespace volcanite
