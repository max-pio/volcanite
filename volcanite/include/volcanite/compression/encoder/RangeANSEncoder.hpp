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

class RangeANSEncoder : public CSGVSerialBrickEncoder {

public:
    RangeANSEncoder(uint32_t brick_size, EncodingMode encoding_mode,
                    const uint32_t code_frequencies[16], const uint32_t detail_code_frequencies[16])
            : CSGVSerialBrickEncoder(brick_size, encoding_mode) {
        if (encoding_mode != SINGLE_TABLE_RANS_ENC && encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("RangeANSEncoder must be used with SINGLE_TABLE_RANS_ENC or DOUBLE_TABLE_RANS_ENC"
                                     " encoding mode.");

        m_rans.recomputeFrequencyTables(code_frequencies);
        if(encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            if (detail_code_frequencies == nullptr)
                throw std::runtime_error("Detail code frequencies must be given if using double table rANS encoding!");
            m_detail_rans.recomputeFrequencyTables(detail_code_frequencies);
        }
    }

    // VARIABLE BIT LENGTH ENCODING ------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<uint32_t> getCurrentFrequencyTable() const {
        std::vector<uint32_t> freq(16);
        m_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    [[nodiscard]] std::vector<uint32_t> getCurrentDetailFrequencyTable() const {
        if (m_encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("Cannot get a detail frequency table from a Compressed Segmentation Volume that's not using rANS in double table mode.");
        std::vector<uint32_t> freq(16);
        m_detail_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string> getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                                                  uint32_t brick_idx_count) const override {
        std::vector<std::string> defines = CSGVSerialBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);

        // build frequency table string
        std::stringstream ss;
        ss << "RANS_SYMBOL_TABLE=uvec3[34](";
        ss << m_rans.getGLSLSymbolArrayString();
        ss << ",";
        if(m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            ss << m_detail_rans.getGLSLSymbolArrayString();
        } else {
            // just some dummy entries so the shader compiles..
            for (int i = 0; i <= 16; i++)
                ss << (i < 16 ? "uvec3(0u, 0u, 0u)," : "uvec3(0u, 0u, 0u)");
        }
        ss << ")";
        defines.push_back(ss.str());

        return defines;
    }

protected:
    /// Reads the next element from the brick encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.
    uint32_t readNextLodOperationFromEncoding(const uint32_t* brick_encoding, ReadState& state) const override;

};

} // namespace volcanite
