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

// this file uses code from Fabien Giesen's CC0 ryg-rans implementation. See the Volcanite dependencies for details.

#ifndef RANS_GLSL
#define RANS_GLSL

#include "volcanite/compression/csgv_utils.glsl"

#define RANS_PROB_BITS 14
#define RANS_BYTE_L (1u << 23)  // lower bound of our normalization interval

// for both the base encoding + for the detail back to back:
// decoding info for all 16 possible symbols: (dsyms.start, dsyms.freq, stats.cum_freq) + a 17th dummy entry with (0, 0, total stats.cum_freq)
const uvec3 _RANS_STATS[34] = RANS_SYMBOL_TABLE;

uint _RansDecGet(uint rans_state) {
    return rans_state & ((1u << RANS_PROB_BITS) - 1u);
}


void _RansDecAdvanceSymbol(inout uint r, in EncodingRef enc_start, inout uint byte_index, uint sym_start, uint sym_freq) {
    const uint mask = (1u << RANS_PROB_BITS) - 1u;

    // s, x = D(x)
    uint x = r;
    x = sym_freq * (x >> RANS_PROB_BITS) + (x & mask) - sym_start;

    // renormalize
    if (x < RANS_BYTE_L) {
        uint idx = byte_index;
        do {
            // read the next byte from our uint32 array
            uint shift = 8 * (idx % 4);
            // TODO: use bitfieldExtract instead of manual bit selection
            uint byte = (enc_start.buf[idx / 4] >> shift) & 0xFFu;
            x = (x << 8) | byte;
            idx++;
        } while (x < RANS_BYTE_L);
        byte_index = idx;
    }

    r = x;
}

void rans_itr_initDecoding(inout uint rans_state, in EncodingRef enc_start, inout uint byte_index) {
    rans_state = enc_start.buf[byte_index/4];
    byte_index += 4;
}

uint rans_itr_nextSymbol(inout uint rans_state, in EncodingRef enc_start, inout uint byte_index, uint freq_table_offset) {
    uint cumulative = _RansDecGet(rans_state);
    uint s;
    for(s=freq_table_offset; s < (freq_table_offset + 16); s++) {
        if(_RANS_STATS[s+1].z > cumulative)
        break;
    }
    _RansDecAdvanceSymbol(rans_state, enc_start, byte_index, _RANS_STATS[s].x, _RANS_STATS[s].y);
    return s - freq_table_offset;
}

#endif
