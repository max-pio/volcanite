#ifndef VOLCANITE_BITVECTOR_GLSL
#define VOLCANITE_BITVECTOR_GLSL

#ifndef BV_WORD_TYPE
    #define BV_WORD_TYPE uint64_t
#endif

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict BitVectorRef
{
    BV_WORD_TYPE words[];
};

#define WORD_ACCESS(word, index) uint(word >> (index % BV_WORD_BIT_SIZE) & 1u)
#define WORD_SET0(word, index) atomicAnd(word, ~(BV_WORD_TYPE(1u) << (index % BV_WORD_BIT_SIZE)) )
#define WORD_SET1(word, index) atomicOr(word, BV_WORD_TYPE(1u) << (index % BV_WORD_BIT_SIZE))
#define WORD_RANK1(word, index) uint((index) != 0u ? bitCount(word << (BV_WORD_BIT_SIZE - (index))) : 0u)


#define BV_ACCESS(bitvector, index) WORD_ACCESS(bitvector[index / BV_WORD_BIT_SIZE], index)
#define BV_SET0(bitvector, index) WORD_SET0(bitvector[index / BV_WORD_BIT_SIZE], index)
#define BV_SET1(bitvector, index) WORD_SET1(bitvector[index / BV_WORD_BIT_SIZE], BV_WORD_TYPE(1u) << (index % BV_WORD_BIT_SIZE))


uint word_access_uvec4(uvec4 word, uint index) {
    return (word[index / 32u] >> (index % 32u) & 1u);
}

uint word_rank1_uvec4(uvec4 word, uint index) {
//    #if (BV_WORD_BIT_SIZE != 32u)
//        STATIC_FAIL("bit vector word size must be 32 when used in uvec4 words");
//    #endif
    const uvec4 offset[4] = { uvec4(0u, 0u, 0u, 0u),
                              uvec4(~0u, 0u, 0u, 0u),
                              uvec4(~0u, ~0u, 0u, 0u),
                              uvec4(~0u, ~0u, ~0u, 0u) };

    uvec4 bitCounts = bitCount(word & offset[index / 32u]);
    return bitCounts.x + bitCounts.y + bitCounts.z + bitCounts.w + bitCount(word[index / 32u] << (index % 32u));
}

#endif // VOLCANITE_BITVECTOR_GLSL
