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
// Parts of this code are based on code from the pasta-toolkit BitVector implementation by Florian Kurpicz which is
// licensed under the GPLv3 license. https://github.com/pasta-toolbox/bit_vector

#ifndef VOLCANITE_BITVECTOR_GLSL
#define VOLCANITE_BITVECTOR_GLSL

// note: we assume that the bit vector word type and the flat rank word type are identical
#ifndef BV_WORD_TYPE
    #define BV_WORD_BIT_SIZE 64
    #define BV_WORD_TYPE uint64_t
#endif

#ifdef EMPTY_SPACE_UINT_SIZE
    #define EMPTY_SPACE_BV_WORD_SIZE (EMPTY_SPACE_UINT_SIZE / (BV_WORD_BIT_SIZE / 32))
    #define EMPTY_SPACE_BV_BIT_SIZE (EMPTY_SPACE_UINT_SIZE * 32)
#endif

#if BV_WORD_TYPE == uint64_t
    #extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
    #extension GL_EXT_shader_atomic_int64 : require
#endif


layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict BitVectorRef
{
    BV_WORD_TYPE words[];
};

#define WORD_ACCESS(word, index) uint(((word) >> (index)) & 1u)
#define WORD_SET0(word, index) atomicAnd((word), ~(BV_WORD_TYPE(1u) << (index)) )
#define WORD_SET1(word, index) atomicOr((word), BV_WORD_TYPE(1u) << (index))
#if BV_WORD_TYPE == uint64_t
    #define WORD_RANK1(word, index) uint((index) != 0u ? bitCount64(word << (BV_WORD_BIT_SIZE - (index))) : 0u)
#elif BV_WORD_TYPE uint
    #define WORD_RANK1(word, index) uint((index) != 0u ? bitCount(word << (BV_WORD_BIT_SIZE - (index))) : 0u)
#else
     #error "unkown bit vector word size"
#endif


#define BV_ACCESS(bitvector, index) WORD_ACCESS(bitvector[(index) / BV_WORD_BIT_SIZE], (index) % BV_WORD_BIT_SIZE)
#define BV_SET0(bitvector, index) WORD_SET0(bitvector[(index) / BV_WORD_BIT_SIZE], (index) % BV_WORD_BIT_SIZE)
#define BV_SET1(bitvector, index) WORD_SET1(bitvector[(index) / BV_WORD_BIT_SIZE], (index) % BV_WORD_BIT_SIZE)

uint bitCount64(uint64_t value) {
    return bitCount(uint(value)) + bitCount(uint(value >> 32));
}

uint64_t bitfieldExtract64(uint64_t value, int offset, int bits) {
    assert(bits < 64 && offset < 64, "bitfieldExtract64 requires offset and bits < 64");
    return (value >> offset) & ((uint64_t(1) << bits) - uint64_t(1));
}

uint word_access_uvec4(uvec4 word, uint index) {
    return (word[index / 32u] >> (index % 32u) & 1u);
}

uint word_rank1_uvec4(uvec4 word, uint index) {
    //    #if (BV_WORD_BIT_SIZE != 32u)
    //        #error "bit vector word size must be 32 when used in uvec4 words";
    //    #endif
    const uvec4 offset[4] = { uvec4(0u, 0u, 0u, 0u),
                              uvec4(~0u, 0u, 0u, 0u),
                              uvec4(~0u, ~0u, 0u, 0u),
                              uvec4(~0u, ~0u, ~0u, 0u) };

    uvec4 bitCounts = bitCount(word & offset[index / 32u]);
    return bitCounts.x + bitCounts.y + bitCounts.z + bitCounts.w + bitCount(word[index / 32u] << (index % 32u));
}


// FLAT RANK -----------------------------------------------------------------------------------------------------------

// assume that flat rank support is enabled when the respective defines exist:
#ifdef BV_STORE_L1_BITS

uint _get_L1_entry(const BV_WORD_TYPE v) {
    return uint(bitfieldExtract64(v, 0, BV_STORE_L1_BITS)); // the least significant BV_STORE_L1_BITS store the L1-information
}

uint _get_L2_entry(const BV_WORD_TYPE v, uint i) {
    // First L2-information is always zero and not stored explicitly. For i > 0, BV_STORE_L2_BITS bits are stored per
    // L2-information (e.g. 9 bits per for all except the first one L2-block each). They are ordered in the BV_L12Type
    // from LSB to MSB, starting after the least significant BV_STORE_L1_BITS bits (e.g. 19) that are used for L1-info.
    const uint OFFSET = BV_STORE_L1_BITS - BV_STORE_L2_BITS;
    return (i != 0u) ? uint(bitfieldExtract64(v, int(i * BV_STORE_L2_BITS + OFFSET), BV_STORE_L2_BITS)) : 0u;
}


uint _fr_rank1(uint index, BitVectorRef bv, BitVectorRef fr) {
    assert(_get_L1_entry(fr.words[0]) == 0u, "corrupted flat rank: first L1 is not 0");
//#if 1
//    uint count = 0u;
//    const uint words = index / BV_WORD_BIT_SIZE;
//    for (uint i = 0; i < words; i++) {
//        count += bitCount64(bv.words[i]);
//    }
//    for (uint i = words * BV_WORD_BIT_SIZE; i < index; i++) {
//        if (BV_ACCESS(bv.words, i) == 1u)
//            count++;
//    }
//    return count;
//#endif
    // ........ ........  bits
    // ┌┐┌┐┌┐┌┐ ┌┐┌┐┌┐┌┐  words
    // └┘└┘└┘└┘ └┘└┘└┘└┘
    // ┌──┐┌──┐ ┌──┐┌──┐  l2-blocks
    // └──┘└──┘ └──┘└──┘
    // ┌──────┐ ┌──────┐  l1-blocks
    // └──────┘ └──────┘

    // query L12 acceleration structure
    BV_WORD_TYPE l12 = fr.words[index / BV_L1_BIT_SIZE];
    uint rank1_res = _get_L1_entry(l12);
    assertf(rank1_res < (index == 0u ? 1u : index),
            "FR_RANK1 _get_L1_entry return value too high. [index, rank1]: [%v2u]",
            uvec2(index, rank1_res));
    rank1_res += _get_L2_entry(l12, (index % BV_L1_BIT_SIZE) / BV_L2_BIT_SIZE);

    // perform bit counts on a word level to count the remaining bits
    uint offset = ((index / BV_WORD_BIT_SIZE) / BV_L2_WORD_SIZE) * BV_L2_WORD_SIZE;
    // fill missing 'full' counted words if L2-blocks cover multiple words
    #if (BV_L2_WORD_SIZE > 1)
        for (uint _w = 0u; _w < ((index / BV_WORD_BIT_SIZE) % BV_L2_WORD_SIZE); _w++) {
            rank1_res += bitCount64(bv.words[offset]);
            offset++;
        }
    #endif
    // if this is a rank(text_size) query, the inlining of the function lead to the potential out of bounds
    // access bv[offset] being ignored.

    assertf(rank1_res + WORD_RANK1(bv.words[offset], index % BV_WORD_BIT_SIZE) < (index == 0u ? 1u : index),
            "FR_RANK1 return value too high. [index, rank1]: [%v2u]",
            uvec2(index, rank1_res + WORD_RANK1(bv.words[offset], index % BV_WORD_BIT_SIZE)));
    return rank1_res + WORD_RANK1(bv.words[offset], index % BV_WORD_BIT_SIZE);
}

#endif // BV_STORE_L1_BITS (Flat Rank)

#endif // VOLCANITE_BITVECTOR_GLSL
