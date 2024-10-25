#ifndef VOLCANITE_BITVECTOR_GLSL
#define VOLCANITE_BITVECTOR_GLSL

#ifndef BV_WORD_TYPE
    #define BV_WORD_TYPE uint64_t
#endif

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict BitVectorRef
{
    BV_WORD_TYPE words[];
};

#define BV_ACCESS(bitvector, index) (bitvector[index / BV_WORD_BIT_SIZE] >> (index % BV_WORD_BIT_SIZE) & 1u);

#define BV_SET0(bitvector, index) { atomicAnd(bitvector[index / BV_WORD_BIT_SIZE], ~(BV_WORD_TYPE(1u) << (index % BV_WORD_BIT_SIZE)) ) }

#define BV_SET1(bitvector, index) atomicOr(bitvector[index / BV_WORD_BIT_SIZE], BV_WORD_TYPE(1u) << (index % BV_WORD_BIT_SIZE))


#endif VOLCANITE_BITVECTOR_GLSL
