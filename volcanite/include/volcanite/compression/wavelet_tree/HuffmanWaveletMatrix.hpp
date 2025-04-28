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

#include "vvv/util/Logger.hpp"
#include "vvv/util/util.hpp"
#include <cassert>

#include "WaveletMatrixBase.hpp"

using namespace vvv;

namespace volcanite {

constexpr static uint32_t HWM_LEVELS = 5u;
constexpr static uint32_t HWM_ALPHABET_SIZE = 6u;
constexpr static uint32_t HWM_MAX_CHC_LENGTH = 5;

struct HuffmanCode {
    uint32_t length;   ///< length of the Huffman code
    uint32_t bit_code; ///< Huffman code stored in the {length} most significant bits with bit 0 being the MSB

    static constexpr uint32_t CHC_BIT_SIZE = sizeof(bit_code) * 8;

    constexpr HuffmanCode(const uint32_t _length, const uint32_t _bits) : length(_length), bit_code(_bits << (CHC_BIT_SIZE - length)) {
        assert(length > 0 && length <= HWM_MAX_CHC_LENGTH && "invalid huffman code length");
    }

    [[nodiscard]] uint32_t getBit(const int i) const {
        assert(i < length && "accessing out of bounds bit of huffman code");
        return (bit_code >> (CHC_BIT_SIZE - 1 - i)) & 1u;
    }
};

class HuffmanWaveletMatrix : public WaveletMatrixBase {

    static_assert(HWM_ALPHABET_SIZE <= (1ULL << HWM_LEVELS), "HWM_ALPHABET_SIZE must be <= 2^{HWM_LEVELS}.");
    // the bit_blocks that are constructed are uint64 currently. for other word sizes, generalize the construction.
    static_assert(BV_WORD_BIT_SIZE == sizeof(uint64_t) * 8u, "Prefix counting wavelet matrix construction unable to"
                                                             " handle bit vector word size.");

  protected:
    BitVector m_bv; ///< Wavelet matrix bit vectors of all 4 levels concatenated.
    FlatRank *m_fr; ///< Flat rank L12-block acceleration structure for rank operations.
    // uint32_t m_zeros_on_level[HWM_LEVELS];    ///< Number of zeros in each level of the wavelet matrix.
    uint32_t m_ones_before[HWM_LEVELS];      ///< Number of ones before each level of the wavelet matrix.
    uint32_t m_level_starts[HWM_LEVELS + 1]; ///< Bit index in the concatenated bit vector at which each level starts

  public:
    /// bit inverted canonical Huffman codes for the 6 operations
    static constexpr HuffmanCode SYMBOL2CHC[6] = {{1, 1},  // 1 000000 PARENT
                                                  {2, 1},  // 01 00000 NEIGHBOR_X
                                                  {3, 1},  // 001 0000 NEIGHBOR_Y
                                                  {4, 1},  // 0001 000 NEIGHBOR_Z
                                                  {5, 1},  // 00001 00 PALETTE_ADV
                                                  {5, 0}}; // 00000 00 PALETTE_LAST
    static uint32_t CHC2SYMBOL(const uint32_t code) { return 4u - glm::findMSB(code); }

    // ATTENTION: this encoder assumes that the chc are obtained with Golomb/Rice coding with M=1 for all symbols except
    // the last two., i.e. can only contain zeros as prefix before its LSB as:
    // 0^{n}1 for n <= HWM_MAX_CHC_LENGTH or 0^{HWM_MAX_CHC_LENGTH}
    // this results in a Huffman shaped wavelet matrix where ANY 1 edge immediately terminates the symbol
    static_assert(std::all_of(HuffmanWaveletMatrix::SYMBOL2CHC, HuffmanWaveletMatrix::SYMBOL2CHC + HWM_ALPHABET_SIZE,
                              [](const auto &chc) {
                                  return chc.length <= HWM_MAX_CHC_LENGTH &&
                                         (std::popcount(chc.bit_code) == 1u || (std::popcount(chc.bit_code) == 0u && chc.length == HWM_MAX_CHC_LENGTH));
                              }),
                  "CHC do not match the criteria by the Huffman Wavelet Matrix Encoder.");

    HuffmanWaveletMatrix(const uint32_t *op_stream_in, uint32_t start4bit, uint32_t end4bit);
    ~HuffmanWaveletMatrix() override { delete m_fr; }

    [[nodiscard]] uint32_t access(uint32_t position) const override;
    [[nodiscard]] uint32_t rank(uint32_t position, uint32_t symbol) const override;

    [[nodiscard]] const BitVector *getBitVector() const override { return &m_bv; }
    [[nodiscard]] const FlatRank *getFlatRank() const override { return m_fr; }

    [[nodiscard]] uint32_t getLevels() const override { return HWM_LEVELS; };
    [[nodiscard]] const uint32_t *getZerosInLevel() const override {
        throw std::runtime_error("HuffmanWaveletMatrix does not provide zeros_on_level");
        // return &m_zeros_on_level[0];
    }
    [[nodiscard]] const uint32_t *getOnesBeforeLevel() const override { return &m_ones_before[0]; }
    [[nodiscard]] const uint32_t *getLevelStarts() const { return &m_level_starts[0]; }

    [[nodiscard]] size_t getByteSize() const override {
        const size_t bytes = (4 + 3 * HWM_LEVELS) * sizeof(uint32_t)             // ones_bef, zeros_on_lvl, lvl_starts, text_size
                       + m_bv.getRawDataSize() * sizeof(BV_WordType)       // bit vector(s) for all levels
                       + m_fr->getRawDataSize() * sizeof(BV_L12Type) + 12; // FlatRank incl. size and data pointer
        return bytes;
    }
};

} // namespace volcanite
