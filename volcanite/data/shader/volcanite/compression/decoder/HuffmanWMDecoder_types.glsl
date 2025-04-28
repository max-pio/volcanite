#ifndef HUFFMAN_WM_DECODER_TYPES_GLSL
#define HUFFMAN_WM_DECODER_TYPES_GLSL

#include "volcanite/compression/csgv_utils.glsl"

// TODO: move bit vector implementations and macros to bit_vector.glsl

// Required DEFINES from shader compiler: (BV_WORD_TYPE must be the same as the BV_L12Type)
#ifndef HWM_LEVELS
    // these defines are only here for intellisense
    #define HWM_LEVELS 5
    #define BV_L1_BIT_SIZE 1280
    #define BV_L2_BIT_SIZE 256
    #define BV_L2_WORD_SIZE 4
    #define BV_STORE_L1_BITS 19
    #define BV_STORE_L2_BITS 11
    #define UINT_PER_L12 2
    ASSERT_FAIL(missing_shader_defines_for_HUFFMAN_WM_ENC);
#endif

// careful! use this only if you're copying data incl. the full fr[] array into it. Otherwise, use WMHBrickHeaderRef.
struct WMHBrickHeader {
    uint bit_vector_size;       ///< symbols in the encoding stream
    uint ones_before_level[5];  ///< number of ones before each level in the wavelet matrix
    uint level_starts_1_to_4[4];///< number of zeros within each level in the wavelet matrix
    BV_WORD_TYPE fr[1];         ///< L12 flat rank acceleration structure (flexible array member)
}; // must be 12x 4 Bytes packed

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict WMHBrickHeaderRef
{
    uint bit_vector_size;       ///< symbols in the encoding stream
    uint ones_before_level[5];  ///< number of ones before each level in the wavelet matrix
    uint level_starts_1_to_4[4];///< number of zeros within each level in the wavelet matrix
    BV_WORD_TYPE fr[1];         ///< L12 flat rank acceleration structure (flexible array member)
};  // must be 12x 4 Bytes packed size

#endif // HUFFMAN_WM_DECODER_TYPES_GLSL
