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

#include "volcanite/compression/encoder/CSGVSerialBrickEncoder.hpp"

namespace volcanite {

class NibbleEncoder : public CSGVSerialBrickEncoder {

public:
    NibbleEncoder(uint32_t brick_size, EncodingMode encoding_mode) : CSGVSerialBrickEncoder(brick_size, encoding_mode) {
        if (encoding_mode != NIBBLE_ENC)
            throw std::runtime_error("NibbleEncoder must be used with NIBBLE_ENC encoding mode.");
    }

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string> getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                                                  uint32_t brick_idx_count) const {
        return CSGVBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);
    }


protected:
    /// Reads the next element from the brick encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.
    uint32_t readNextLodOperationFromEncoding(const uint32_t* brick_encoding, ReadState& state) const override;
};

} // namespace volcanite
