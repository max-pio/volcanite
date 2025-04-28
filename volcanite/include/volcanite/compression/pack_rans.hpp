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
// This file contains large parts of the CC0 licensed rANS implementation by Fabian Giesen, see
// https://github.com/rygorous/ryg_rans

#pragma once

#include "vvv/util/Logger.hpp"
#include "vvv/util/util.hpp"
#include <cassert>

#include "ryg_rans/rans_nibble.h"

using namespace vvv;

namespace volcanite {

#define RANS_ALPHABET_SIZE 16

// ---- Stats
struct SymbolStats {
    uint32_t freqs[RANS_ALPHABET_SIZE];
    uint32_t cum_freqs[RANS_ALPHABET_SIZE + 1];

    void count_freqs(uint8_t const *in, size_t nbytes);
    void calc_cum_freqs();
    void normalize_freqs(uint32_t target_total);
};

class RANS {

  private:
    static const uint32_t prob_bits = 14;
    static const uint32_t prob_scale = 1 << prob_bits;

    RansEncSymbol esyms[RANS_ALPHABET_SIZE];
    RansDecSymbol dsyms[RANS_ALPHABET_SIZE];
    uint8_t cum2sym[prob_scale];
    SymbolStats stats;
    bool has_frequency_tables = false;

  public:
    explicit RANS(const uint32_t *frequency_array = nullptr) {
        if (frequency_array)
            recomputeFrequencyTables(frequency_array);
    }

    void recomputeFrequencyTables(const uint32_t *frequency_array = nullptr);
    void recomputeFrequencyTables(std::vector<uint8_t> &in_bytes);

    void copyCurrentFrequencyTableTo(uint32_t *frequency_array) const {
        assert(has_frequency_tables && "can't copy frequency table because it doesn't exist");
        assert(frequency_array && "invalid pointer to write buffer for frequency array");
        for (int i = 0; i < 16; i++)
            frequency_array[i] = stats.freqs[i];
    }

    /**
     * Replaces all 4 bit elements between start4bit (including) and end4bit (excluding)  in_packed with a rANS encoded bytestream.
     * The first 4 bit element start4bit must be the first position in a 32bit memory location.
     * @return the new end4bit endpoint measured in number of 4 bit elements
     */
    uint32_t packRANS(std::vector<uint32_t> &in_packed, uint32_t start4bit, uint32_t end4bit) const;

    uint32_t packRANS(uint32_t *in_packed, uint32_t start4bit, uint32_t end4bit) const;

    /**
     * Decodes number_of_out_elements packed half bytes to the byte array starting at out. Out will have half the size of the actual elements (since it is a vector of bytes instead of half bytes).
     */
    int unpackRANS(const uint8_t *rans_begin, uint8_t *out, size_t number_of_output_elements) const;

    /**
     * Initializes iterative decoding for reading single elements from the decoding with itr_nextSymbol(). The state is carried in both of the parameters.
     */
    void itr_initDecoding(uint32_t *rans_state, const uint8_t **rans_ptr) const;
    void itr_initDecoding(uint32_t &rans_state, uint32_t &byte_index, const uint32_t *array) const;
    /**
     * Returns the next element from the decoding given the current internal state and updates the state in the two parameters.
     */
    uint32_t itr_nextSymbol(uint32_t *rans_state, const uint8_t **rans_ptr) const;
    uint32_t itr_nextSymbol(RansState &rans_state, uint32_t &byte_index, const uint32_t *array) const;

    std::vector<uint32_t> getFrequencyArray() {
        assert(has_frequency_tables && "rANS instance has no frequency array");
        std::vector<uint32_t> out(16);
        for (int i = 0; i < 16; i++)
            out[i] = stats.freqs[i];
        return out;
    }
    /**
     * @return an array string "uvec3[17](uvec3(..), ..)" with 17 elements where each element < 16 is (dsys.start, dsyms.freq, stats.cum_freq) and element 16 is (0, 0, stats.cum_freq[16]).
     */
    std::string getGLSLSymbolArrayString() const {
        // output our frequency tables:
        std::stringstream ss;
        // added in CompressedSegmentationVolume: ss << "uvec3[17](";
        for (int i = 0; i < RANS_ALPHABET_SIZE; i++) {
            ss << "uvec3(" << dsyms[i].start << "," << dsyms[i].freq << "," << stats.cum_freqs[i] << "),";
        }
        ss << "uvec3(0,0," << stats.cum_freqs[RANS_ALPHABET_SIZE] << ")";
        // added in CompressedSegmentationVolume:  )";
        return ss.str();
    }
};

} // namespace volcanite
