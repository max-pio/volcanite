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
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"

namespace volcanite {

    struct WMBrickHeader {
        uint32_t text_size;            ///< symbols in the encoding stream
        glm::uvec4 ones_before_level;  ///< number of ones before each level in the wavelet matrix
        glm::uvec4 zeros_on_level;     ///< number of zeros within each level in the wavelet matrix
        const BV_L12Type* fr;          ///< L12 flat rank acceleration structure
        const BV_WordType* bv;         ///< bit vector containing bit vectors of all wavelet matrix levels concatenated
    };


    /// @param base_header_size the number of initial uint32 header elements that are not WM specific
    WMBrickHeader getWMBrickHeaderFromEncoding(const uint32_t* v, uint32_t base_header_size);

    /// Replaces all 4 bit elements between start4bit (including) and end4bit (excluding) in in_packed with a
    /// wavelet matrix encoded bytestream. Updates the brick header's start position at v[0] to point to the beginning
    /// of the FlatRank acceleration of the WaveletMatrix stream. The new layout is:\n
    /// [old header] [text size] [4x ones before level] [4x zeros in level] | 64b[flat rank] 64b[bit vectors]
    /// The first 4 bit element start4bit must be the first position in a 32bit memory location.
    /// The first lod_count header entries are adapted to store the start indices of LODs as operation counts with
    /// v[0] = 0 being the start operation count of the first LOD.
    /// @return the new end4bit endpoint measured in number of 4 bit elements
    uint32_t packWaveletMatrix(uint32_t* v, std::size_t start4bit, std::size_t end4bit, uint32_t lod_count);

    // WAVELET MATRIX ACCESS AND RANK ==================================================================================

    uint32_t wm_access(uint32_t position, const WMBrickHeader& wm_header);
    uint32_t wm_rank(uint32_t position, uint32_t symbol, const WMBrickHeader& wm_header);

}