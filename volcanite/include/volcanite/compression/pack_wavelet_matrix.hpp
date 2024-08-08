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

#pragma once

#include <cstdint>
#include <vector>

namespace volcanite {

    /// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
    /// wavelet matrix encoded bytestream.
    /// The first 4 bit element start4bit must be the first position in a 32bit memory location.
    /// @return the new end4bit endpoint measured in number of 4 bit elements
    uint32_t packWaveletMatrix(std::vector<uint32_t> &v, std::size_t start, std::size_t end) {
        // Construct a temporary WaveletMatrix Object from the input stream
        // Then overwrite the vector contents with the wavelet matrix components as follows:
        // uint32_t Text Size | 4x uint32 ones before level | 4x uint32 zeros in level | FlatRank | Bit Vector
        // (overwrite header start indices, LOD 0) ?
        // return length of vector (which can be longer than before!)
    }

    uint32_t accessWMSymbol(uint32_t index, uint32_t text_size, uint64_t* bit_vector, uint64_t* FlatRank,
                          uint32_t ones_before_level[4], uint32_t zeros_in_level[4]) {
        // TODO: copy the WaveletMatrix access / [] code here
    }

    uint32_t rankWSSymbol(uint32_t symbol, int32_t text_size, uint64_t* bit_vector, uint64_t* FlatRank,
                          uint32_t ones_before_level[4], uint32_t zeros_in_level[4]) {
        // TODO: copy the WaveletMatrix rank code here
    }

}