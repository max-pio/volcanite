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

#include "volcanite/compression/pack_rans.hpp"

using namespace vvv;

namespace volcanite {

void SymbolStats::count_freqs(uint8_t const *in, const size_t nbytes) {
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++)
        freqs[i] = 0;

    for (size_t i = 0; i < nbytes; i++)
        freqs[in[i]]++;
}

void SymbolStats::calc_cum_freqs() {
    cum_freqs[0] = 0;
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++)
        cum_freqs[i + 1] = cum_freqs[i] + freqs[i];
}

void SymbolStats::normalize_freqs(const uint32_t target_total) {
    assert(target_total >= 256); // >= RANS_ALPHABET_SIZE?

    calc_cum_freqs();
    const uint32_t cur_total = cum_freqs[RANS_ALPHABET_SIZE];

    // resample distribution based on cumulative freqs
    for (int i = 1; i <= RANS_ALPHABET_SIZE; i++)
        cum_freqs[i] = ((uint64_t)target_total * cum_freqs[i]) / cur_total;

    // if we nuked any non-0 frequency symbol to 0, we need to steal
    // the range to make the frequency nonzero from elsewhere.
    //
    // this is not at all optimal, i'm just doing the first thing that comes to mind.
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++) {
        if (freqs[i] && cum_freqs[i + 1] == cum_freqs[i]) {
            // symbol i was set to zero freq

            // find best symbol to steal frequency from (try to steal from low-freq ones)
            uint32_t best_freq = ~0u;
            int best_steal = -1;
            for (int j = 0; j < RANS_ALPHABET_SIZE; j++) {
                uint32_t freq = cum_freqs[j + 1] - cum_freqs[j];
                if (freq > 1 && freq < best_freq) {
                    best_freq = freq;
                    best_steal = j;
                }
            }
            assert(best_steal != -1);

            // and steal from it!
            if (best_steal < i) {
                for (int j = best_steal + 1; j <= i; j++)
                    cum_freqs[j]--;
            } else {
                assert(best_steal > i);
                for (int j = i + 1; j <= best_steal; j++)
                    cum_freqs[j]++;
            }
        }
    }

    // calculate updated freqs and make sure we didn't screw anything up
    assert(cum_freqs[0] == 0 && cum_freqs[RANS_ALPHABET_SIZE] == target_total);
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++) {
        if (freqs[i] == 0)
            assert(cum_freqs[i + 1] == cum_freqs[i]);
        else
            assert(cum_freqs[i + 1] > cum_freqs[i]);

        // calc updated freq
        freqs[i] = cum_freqs[i + 1] - cum_freqs[i];
    }
}

void RANS::recomputeFrequencyTables(const uint32_t *frequency_array) {
    assert(frequency_array && "no frequency array given");
    stats = SymbolStats();
    for (int s = 0; s < RANS_ALPHABET_SIZE; s++)
        stats.freqs[s] = frequency_array[s];
    stats.normalize_freqs(prob_scale);

    for (int s = 0; s < RANS_ALPHABET_SIZE; s++) {
        for (uint32_t i = stats.cum_freqs[s]; i < stats.cum_freqs[s + 1]; i++)
            cum2sym[i] = s;
    }

    // build the symbol tables
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++) {
        RansEncSymbolInit(&esyms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);
        RansDecSymbolInit(&dsyms[i], stats.cum_freqs[i], stats.freqs[i]);
    }

    has_frequency_tables = true;
}

void RANS::recomputeFrequencyTables(std::vector<uint8_t> &in_bytes) {
    stats = SymbolStats();
    stats.count_freqs(in_bytes.data(), in_bytes.size());
    stats.normalize_freqs(prob_scale);

    // cumulative->symbol table, this is super brute force
    std::stringstream sss("");
    for (int s = 0; s < RANS_ALPHABET_SIZE; s++) {
        for (uint32_t i = stats.cum_freqs[s]; i < stats.cum_freqs[s + 1]; i++)
            cum2sym[i] = s;
    }

    // build the symbol tables
    for (int i = 0; i < RANS_ALPHABET_SIZE; i++) {
        RansEncSymbolInit(&esyms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);
        RansDecSymbolInit(&dsyms[i], stats.cum_freqs[i], stats.freqs[i]);
    }

    has_frequency_tables = true;
}

uint32_t RANS::packRANS(std::vector<uint32_t> &in_packed, const uint32_t start4bit, const uint32_t end4bit) const {
    assert(has_frequency_tables && "no frequency tables are given!");

    const size_t out_max_size = 8u + (end4bit - start4bit) * 2; // assume worst case compression rate of 100% (measured in bytes this is twice the 4bit count) 32 << 20; // 32MB
    uint8_t *out_buf = new uint8_t[out_max_size];

    // rANS encode --------------------------------------------------------------
    RansState rans;
    RansEncInit(&rans);
    uint8_t *ptr = out_buf + out_max_size;                 // *end* of output buffer
    for (uint32_t i = end4bit - 1u; i >= start4bit; i--) { // NB: working in reverse!
        // 8 half byte elements (each 4 bit large) are packed into one of the uints
        const uint32_t shift = 28u - 4u * (i % 8u);
        const uint32_t s = (in_packed[i / 8u] >> shift) & 0xFu;
        assert(ptr >= out_buf && "out_buf not big enough to store full rANS encoding!");
        RansEncPutSymbol(&rans, &ptr, &esyms[s]);
    }
    RansEncFlush(&rans, &ptr);
    assert(ptr >= out_buf && "out_buf not big enough to store full rANS encoding!");
    const uint8_t *rans_begin = ptr;

    const int new_size_in_bytes = static_cast<int>(out_buf + out_max_size - rans_begin);

    // copy our bytes to the 32bit = 4byte element input array
    assert(start4bit % 8u == 0u && "memory region that we pack in rANS must start at a clean 32bit location");

    const uint32_t start32bit = start4bit / 8u;
    const uint32_t end32bit = start32bit + (new_size_in_bytes + sizeof(uint32_t) - 1) / sizeof(uint32_t); // round our byte_count up so we use exactly a number of uint32_ts
    uint32_t out = 0u;
    uint32_t i = 0;
    for (; i < new_size_in_bytes; i++) {
        const uint32_t shift = 8u * (i % 4); // 4 byte in one uint32_t
        out |= static_cast<uint32_t>(rans_begin[i]) << shift;
        // every 4 elements: write out
        if (i % 4 == 3) {
            in_packed[start32bit + i / 4] = out;
            out = 0u;
        }
    }
    if (i % 4 > 0) {
        in_packed[start32bit + i / 4] = out;
        i += 4;
    }
    assert(new_size_in_bytes < out_max_size && "over capacity");
    assert(start32bit + i / 4 == end32bit && "didn't fill all elements in in_packed");

    delete[] out_buf;
    // return the new end4bit (= end point in number of 4 bit elements)
    return end32bit * 8;
}

uint32_t RANS::packRANS(uint32_t *in_packed, const uint32_t start4bit, const uint32_t end4bit) const {
    assert(has_frequency_tables && "no frequency tables are given!");

    size_t out_max_size = 8u + (end4bit - start4bit) * 2; // assume worst case compression rate of 100% (measured in bytes this is twice the 4bit count) 32 << 20; // 32MB
    uint8_t *out_buf = new uint8_t[out_max_size];

    // rANS encode --------------------------------------------------------------
    RansState rans;
    RansEncInit(&rans);
    uint8_t *ptr = out_buf + out_max_size;                 // *end* of output buffer
    for (uint32_t i = end4bit - 1u; i >= start4bit; i--) { // NB: working in reverse!
        // 8 half byte elements (each 4 bit large) are packed into one of the uints
        const uint32_t shift = 28u - 4u * (i % 8u);
        const uint32_t s = (in_packed[i / 8u] >> shift) & 0xFu;
        assert(ptr >= out_buf && "out_buf not big enough to store full rANS encoding!");
        RansEncPutSymbol(&rans, &ptr, &esyms[s]);
    }
    RansEncFlush(&rans, &ptr);
    assert(ptr >= out_buf && "out_buf not big enough to store full rANS encoding!");
    const uint8_t *rans_begin = ptr;

    const int new_size_in_bytes = static_cast<int>(out_buf + out_max_size - rans_begin);

    // copy our bytes to the 32bit = 4byte element input array
    assert(start4bit % 8u == 0u && "memory region that we pack in rANS must start at a clean 32bit location");

    const uint32_t start32bit = start4bit / 8u;
    const uint32_t end32bit = start32bit + (new_size_in_bytes + sizeof(uint32_t) - 1) / sizeof(uint32_t); // round our byte_count up so we use exactly a number of uint32_ts
    uint32_t out = 0u;
    uint32_t i = 0;
    for (; i < new_size_in_bytes; i++) {
        const uint32_t shift = 8u * (i % 4); // 4 byte in one uint32_t
        out |= static_cast<uint32_t>(rans_begin[i]) << shift;
        // every 4 elements: write out
        if (i % 4 == 3) {
            in_packed[start32bit + i / 4] = out;
            out = 0u;
        }
    }
    if (i % 4 > 0) {
        in_packed[start32bit + i / 4] = out;
        i += 4;
    }
    assert(new_size_in_bytes < out_max_size && "over capacity");
    assert(start32bit + i / 4 == end32bit && "didn't fill all elements in in_packed");

    delete[] out_buf;
    // return the new end4bit (= end point in number of 4 bit elements)
    return end32bit * 8;
}

int RANS::unpackRANS(const uint8_t *rans_begin, uint8_t *out, const size_t number_of_output_elements) const {
    // rANS decode --------------------------------------------------------------
    const uint8_t *ptr = rans_begin;
    RansState rans;
    RansDecInit(&rans, &ptr);
    for (size_t i = 0; i < number_of_output_elements; i++) {
        const uint32_t s = cum2sym[RansDecGet(&rans, prob_bits)];
        if (i % 2 == 0)
            out[i] = (uint8_t)s << 4;
        else
            out[i] |= (uint8_t)s;
        RansDecAdvanceSymbol(&rans, &ptr, &dsyms[s], prob_bits);
    }

    return static_cast<int>(number_of_output_elements / 2);
}

void RANS::itr_initDecoding(uint32_t *rans_state, const uint8_t **rans_ptr) const {
    RansDecInit(rans_state, rans_ptr);
}

void RANS::itr_initDecoding(uint32_t &rans_state, uint32_t &byte_index, const uint32_t *array) const {
    RansDecInit(rans_state, byte_index, array);
}

uint32_t RANS::itr_nextSymbol(uint32_t *rans_state, const uint8_t **rans_ptr) const {
    const uint32_t s = cum2sym[RansDecGet(rans_state, prob_bits)];
    RansDecAdvanceSymbol(rans_state, rans_ptr, &dsyms[s], prob_bits);
    return s;
}

uint32_t RANS::itr_nextSymbol(RansState &rans_state, uint32_t &byte_index, const uint32_t *array) const {
    const uint32_t cumulative = RansDecGet(rans_state, prob_bits);
    // uint32_t s = cum2sym[cumulative];
    uint32_t s;
    for (s = 0; s < 16; s++) {
        if (stats.cum_freqs[s + 1] > cumulative)
            break;
    }
    RansDecAdvanceSymbol(rans_state, byte_index, array, dsyms[s].start, dsyms[s].freq, prob_bits);
    return s;
}

} // namespace volcanite
