#ifndef RANS_GLSL
#define RANS_GLSL

#include "csgv_utils.glsl"

#define RANS_PROB_BITS 14
#define RANS_BYTE_L (1u << 23)  // lower bound of our normalization interval

// for both the base encoding + for the detail back to back:
// decoding info for all 16 possible symbols: (dsyms.start, dsyms.freq, stats.cum_freq) + a 17th dummy entry with (0, 0, total stats.cum_freq)
const uvec3 _RANS_STATS[34] = RANS_SYMBOL_TABLE;

uint _RansDecGet(uint rans_state) {
    return rans_state & ((1u << RANS_PROB_BITS) - 1u);
}


void _RansDecAdvanceSymbol(inout uint r, in uint brick_start, inout uint byte_index, uint sym_start, uint sym_freq, bool detail) {
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
            // ToDo: use bitfieldExtract instead of manual bit selection
#ifdef SEPARATE_DETAIL
            uint byte = (detail ? (CSGV_DETAIL_ARRAY[brick_start + idx / 4] >> shift) : (CSGV_ENCODING_ARRAY[brick_start + idx / 4] >> shift)) & 0xFFu;
#else
            uint byte = (CSGV_ENCODING_ARRAY[brick_start + idx / 4] >> shift) & 0xFFu;
#endif
            x = (x << 8) | byte;
            idx++;
        } while (x < RANS_BYTE_L);
        byte_index = idx;
    }

    r = x;

    assert(((byte_index / 4) + brick_start) >= brick_start, "Buffer index overflow in rANS");
}

void rans_itr_initDecoding(inout uint rans_state, in uint brick_start, inout uint byte_index) {
    rans_state = CSGV_ENCODING_ARRAY[brick_start + byte_index/4];
    byte_index += 4;
}

uint rans_itr_nextSymbol(inout uint rans_state, in uint brick_start, inout uint byte_index, uint freq_table_offset) {
    uint cumulative = _RansDecGet(rans_state);
    uint s;
    for(s=freq_table_offset; s < (freq_table_offset + 16); s++) {
        if(_RANS_STATS[s+1].z > cumulative)
        break;
    }
    _RansDecAdvanceSymbol(rans_state, brick_start, byte_index, _RANS_STATS[s].x, _RANS_STATS[s].y, freq_table_offset > 0u);
    return s - freq_table_offset;
}

#ifdef SEPARATE_DETAIL
void detail_rans_itr_initDecoding(inout uint rans_state, in uint brick_start, inout uint byte_index) {
    // only when using detail separation (which is always used in combination with double table rANS), we have
    // to initialize the rANS decoder for the finest LOD on the detail encoding array
    rans_state = CSGV_DETAIL_ARRAY[brick_start + byte_index/4];
    byte_index += 4;
}
#endif

#endif