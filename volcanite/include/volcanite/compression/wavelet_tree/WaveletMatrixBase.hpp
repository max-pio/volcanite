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
#include <cstring>

#include "BitVector.hpp"

using namespace vvv;

namespace volcanite {

class WaveletMatrixBase {

  protected:
    uint32_t m_text_size;

  public:
    WaveletMatrixBase(const uint32_t *op_stream_in, const uint32_t start4bit, const uint32_t end4bit)
        : m_text_size(end4bit - start4bit) {}
    virtual ~WaveletMatrixBase() = default;

    [[nodiscard]] virtual uint32_t access(uint32_t position) const = 0;
    [[nodiscard]] virtual uint32_t rank(uint32_t position, uint32_t symbol) const = 0;

    [[nodiscard]] uint32_t getTextSize() const { return m_text_size; }
    [[nodiscard]] virtual const BitVector *getBitVector() const = 0;
    [[nodiscard]] virtual const FlatRank *getFlatRank() const = 0;

    [[nodiscard]] virtual uint32_t getLevels() const = 0;
    [[nodiscard]] virtual const uint32_t *getZerosInLevel() const = 0;
    [[nodiscard]] virtual const uint32_t *getOnesBeforeLevel() const = 0;

    [[nodiscard]] virtual size_t getByteSize() const = 0;
};

} // namespace volcanite
