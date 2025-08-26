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
    RangeANSEncoder(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask)
        : CSGVSerialBrickEncoder(brick_size, encoding_mode, op_mask) {
        if (encoding_mode != SINGLE_TABLE_RANS_ENC && encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("NibbleEncoder must be used with SINGLE_TABLE_RANS or DOUBLE_TABLE_RANS"
                                     " encoding mode.");
        m_rans_initialized = false;
    }

    RangeANSEncoder(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask,
                    const uint32_t code_frequencies[16], const uint32_t detail_code_frequencies[16])
        : CSGVSerialBrickEncoder(brick_size, encoding_mode, op_mask) {
        if (encoding_mode != SINGLE_TABLE_RANS_ENC && encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("NibbleEncoder must be used with SINGLE_TABLE_RANS or DOUBLE_TABLE_RANS"
                                     " encoding mode.");

        m_rans.recomputeFrequencyTables(code_frequencies);
        if (encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            if (detail_code_frequencies == nullptr)
                throw std::runtime_error("Detail code frequencies must be given if using double table rANS encoding!");
            m_detail_rans.recomputeFrequencyTables(detail_code_frequencies);
        }
        m_rans_initialized = true;
    }

    // VARIABLE BIT LENGTH ENCODING ------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<uint32_t> getCurrentFrequencyTable() const {
        assert(m_rans_initialized);

        std::vector<uint32_t> freq(16);
        m_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    [[nodiscard]] std::vector<uint32_t> getCurrentDetailFrequencyTable() const {
        assert(m_rans_initialized);

        if (m_encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("Can't get a detail frequency table from a Compressed Segmentation Volume that's not using rANS in double table mode.");
        std::vector<uint32_t> freq(16);
        m_detail_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    // FILE IMPORT AND EXPORT ------------------------------------------------------------------------------------------

    void exportToFile(std::ostream &out) override {
        CSGVSerialBrickEncoder::exportToFile(out);

        auto freq_table = getCurrentFrequencyTable();
        for (int i = 0; i < 16; i++)
            out.write(reinterpret_cast<char *>(&freq_table[i]), sizeof(uint32_t));
        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            auto detail_freq_table = getCurrentDetailFrequencyTable();
            for (int i = 0; i < 16; i++)
                out.write(reinterpret_cast<char *>(&detail_freq_table[i]), sizeof(uint32_t));
        }
    }

    bool importFromFile(std::istream &in) override {
        if (!CSGVSerialBrickEncoder::importFromFile(in))
            return false;

        uint32_t code_frequencies[16];
        uint32_t detail_code_frequencies[16];
        for (int i = 0; i < 16; i++)
            in.read(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(uint32_t));
        m_rans.recomputeFrequencyTables(code_frequencies);
        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            for (int i = 0; i < 16; i++)
                in.read(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(uint32_t));
            m_detail_rans.recomputeFrequencyTables(detail_code_frequencies);
        }

        m_rans_initialized = true;
        return true;
    }

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] std::vector<std::string> getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                                          uint32_t brick_idx_count) const override {
        std::vector<std::string> defines = CSGVSerialBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);

        // build frequency table string
        std::stringstream ss;
        ss << "RANS_SYMBOL_TABLE=uvec3[34](";
        ss << m_rans.getGLSLSymbolArrayString();
        ss << ",";
        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
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
    uint32_t readNextLodOperationFromEncoding(const uint32_t *brick_encoding, ReadState &state) const override;
};

} // namespace volcanite
