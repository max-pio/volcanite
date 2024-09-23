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

#ifndef HUFFMAN_WM_DECODER_GLSL
#define HUFFMAN_WM_DECODER_GLSL

#include "volcanite/compression/csgv_utils.glsl"

/// This Decoder corresponds to the huffman-shaped wavelet matrix encoder from
/// ../../../../include/volcanite/compression/encoder/WaveletMatrixDecoder.hpp
/// and
/// ../../../../include/volcanite/compression/pack_wavelet_matrix.hpp
/// Supported configuration compile time defines:
///   RANDOM_ACCESS

#if ENCODING_MODE != HUFFMAN_WM_ENC
    STATIC_FAIL(expected_HUFFMAN_WM_ENC_encoding_mode);
#endif

#ifdef SEPARATE_DETAIL
    STATIC_FAIL(wavelet_matrix_does_not_support_detail_separation);
#endif

#ifdef PALETTE_CACHE
    STATIC_FAIL(wavelet_matrix_does_not_support_palletized_cache);
#endif

#ifndef RANDOM_ACCESS
    STATIC_FAIL(wavelet_matrix_only_supports_random_access);
#endif

//#define SAFE_BIT_VECTOR


// TYPE DEFINITIONS AND TYPE ACCESS ------------------------------------------------------------------------------------

// Required DEFINES from shader compiler:
// BV_WORD_TYPE (must be the same as the BV_L12Type)
// HWM_LEVELS
// BV_L1_BIT_SIZE
// BASE_HEADER_SIZE 8
// UINT_PER_L12 = sizeof(BV_L12Type)/sizeof(uint)
#ifndef BV_WORD_TYPE
    #define BV_WORD_TYPE uint64_t
    #define HWM_LEVELS 5
    #define BV_L1_BIT_SIZE 128
    #define BV_L2_BIT_SIZE 256
    #define BV_L2_WORD_SIZE 4
    #define BV_STORE_L1_BITS 19
    #define BV_STORE_L2_BITS 11
    #define BASE_HEADER_SIZE 10
    #define UINT_PER_L12 2
    ASSERT_FAIL(missing_shader_defines_for_HUFFMAN_WM_ENC);
#endif

#define CHC_BIT_SIZE 32
//struct HuffmanCode {
//    uint length;            ///< length of the Huffman code
//    uint bit_code;          ///< Huffman code stored in the {length} most significant bits with bit 0 being the MSB
//};
const uvec2 SYMBOL2CHC[6] = {{1, 2147483648}, // 1 000000 ... PARENT
                             {2, 1073741824}, // 01 00000 ... NEIGHBOR_X
                             {3, 536870912},  // 001 0000 ... NEIGHBOR_Y
                             {4, 268435456},  // 0001 000 ... NEIGHBOR_Z
                             {5, 134217728},  // 00001 00 ... PALETTE_ADV
                             {5, 0}};         // 00000 00 ... PALETTE_LAST

struct WMHBrickHeader {
    uint bit_vector_size;       ///< symbols in the encoding stream
    uint ones_before_level[5];  ///< number of ones before each level in the wavelet matrix
    uint level_starts_1_to_4[4];///< number of zeros within each level in the wavelet matrix
    uint64_t fr[1];             ///< L12 flat rank acceleration structure (flexible array member)
}; // must be 12x 4 Bytes packed

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict WMHBrickHeaderRef
{
    WMHBrickHeader header;
};

layout(std430, buffer_reference, buffer_reference_align = 4) buffer readonly restrict BitVectorRef
{
    BV_WORD_TYPE words[];
};

WMHBrickHeaderRef getWMHBrickHeaderFromEncoding(EncodingRef brick_encoding) {
    return WMHBrickHeaderRef(bufferAddressAdd(uvec2(brick_encoding), BASE_HEADER_SIZE));
}

BitVectorRef getWMHBitVectorFromEncoding(EncodingRef brick_encoding) {
    return BitVectorRef(bufferAddressAdd(uvec2(brick_encoding),
                                            BASE_HEADER_SIZE + 10          // base header + constant parts of WMH header
                                            + UINT_PER_L12 * (brick_encoding.buf[BASE_HEADER_SIZE] / BV_L1_BIT_SIZE + 1u)));      // flat rank
}

// UTILITY FUNCTIONS ---------------------------------------------------------------------------------------------------

uint bitCount64(uint64_t value) {
    return bitCount(uint(value)) + bitCount(uint(value >> 32));
}

uint64_t bitfieldExtract64(uint64_t value, int offset, int bits) {
    assert(bits < 64 && offset < 64, "bitfieldExtract64 requires offset and bits < 64");
    return (value >> offset) & ((uint64_t(1) << bits) - uint64_t(1));
}

uint rank1Word(BV_WORD_TYPE value, uint index) {
    return (index != 0u) ? bitCount64(value << (BV_WORD_BIT_SIZE - index)) : 0u;
}

bool TEST_BIT_VECTOR() {
    BV_WORD_TYPE v = uint64_t(12751266098003836929ul);
    // 1011 0000 1111 0101 1001 1000 1111 0101:0000 0000 0000 0000 0000 0000 0000 0001
    //   60   56   52   48   44   40   36   32   28   24   20   16   12    8    4    0
    // bitCount = 19

    assertf(bitCount64(v) == 19, "wrong bitcount is %u", bitCount64(v));
    //
    assertf(rank1Word(v, 0) == 0, "wrong rank1Word 0 is %u", rank1Word(v, 0));
    assertf(rank1Word(v, 1) == 1, "wrong rank1Word 1 is %u", rank1Word(v, 1));
    assertf(rank1Word(v, 32) == 1, "wrong rank1Word 32 is %u", rank1Word(v, 32));
    assertf(rank1Word(v, 33) == 2, "wrong rank1Word 33 is %u", rank1Word(v, 33));
    assertf(rank1Word(v, 63) == 18, "wrong rank1Word 63 is %u", rank1Word(v, 63));
    assertf(rank1Word(v, 64) == 19, "wrong rank1Word 64 is %u", rank1Word(v, 64));
    //
    assertf(bitfieldExtract64(v, 0, 0) == 0, "wrong bitfieldExtract 0 0 is %u", bitfieldExtract64(v, 0, 0));
    assertf(bitfieldExtract64(v, 3, 30) == 536870912u, "wrong bitfieldExtract 3 30 is %u", bitfieldExtract64(v, 3, 30));
    assertf(bitfieldExtract64(v, 32, 13) == 6389, "wrong bitfieldExtract 32 13 is %u", bitfieldExtract64(v, 32, 13));
    assertf(bitfieldExtract64(v, 56, 8) == 176, "wrong bitfieldExtract 56 8 is %u", bitfieldExtract64(v, 56, 8));
    return true;
}

uint getL1Entry(const BV_WORD_TYPE v) {
    return uint(bitfieldExtract64(v, 0, BV_STORE_L1_BITS)); // the least significant BV_STORE_L1_BITS store the L1-information
}

uint getL2Entry(const BV_WORD_TYPE v, uint i) {
    // First L2-information is always zero and not stored explicitly. For i > 0, BV_STORE_L2_BITS bits are stored per
    // L2-information (e.g. 9 bits per for all except the first one L2-block each). They are ordered in the BV_L12Type
    // from LSB to MSB, starting after the least significant BV_STORE_L1_BITS bits (e.g. 19) that are used for L1-info.
    const uint OFFSET = BV_STORE_L1_BITS - BV_STORE_L2_BITS;
    return (i != 0u) ? uint(bitfieldExtract64(v, int(i * BV_STORE_L2_BITS + OFFSET), BV_STORE_L2_BITS)) : 0u;
}

uint _bv_access(uint index, const BitVectorRef bv) {
    return uint(bv.words[index / BV_WORD_BIT_SIZE] >> index) & 1u;
    // bitfieldExtract does not support 64 bit integers:
    // return bitfieldExtract(bv.words[index / BV_WORD_BIT_SIZE], int(index % BV_WORD_BIT_SIZE), 1);
}

uint _fr_rank1(uint index, const BitVectorRef bv, const WMHBrickHeaderRef wm_header) {
//#if 1
//        uint count = 0u;
//        const uint words = index / BV_WORD_BIT_SIZE;
//        for (uint i = 0; i < index / BV_WORD_BIT_SIZE; i++) {
//            count += bitCount64(bv.words[i]);
//        }
//        for (uint i = words * BV_WORD_BIT_SIZE; i < index; i++) {
//            if (_bv_access(i, bv) == 1u)
//                count++;
//        }
//        return count;
//#endif
        // ........ ........  bits
        // ┌┐┌┐┌┐┌┐ ┌┐┌┐┌┐┌┐  words
        // └┘└┘└┘└┘ └┘└┘└┘└┘
        // ┌──┐┌──┐ ┌──┐┌──┐  l2-blocks
        // └──┘└──┘ └──┘└──┘
        // ┌──────┐ ┌──────┐  l1-blocks
        // └──────┘ └──────┘

        // query L12 acceleration structure
        BV_WORD_TYPE l12 = wm_header.header.fr[index / BV_L1_BIT_SIZE];
        uint rank1_res = getL1Entry(l12);
        assertf(rank1_res < (index == 0u ? 1u : index),
                "_fr_rank1 getL1Entry return value too high. [index, rank1]: [%v2u]",
                uvec2(index, rank1_res));
        rank1_res += getL2Entry(l12, (index % BV_L1_BIT_SIZE) / BV_L2_BIT_SIZE);

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

        assertf(rank1_res + rank1Word(bv.words[offset], index % BV_WORD_BIT_SIZE) < (index == 0u ? 1u : index),
                "_fr_rank1 return value too high. [index, rank1]: [%v2u]",
                uvec2(index, rank1_res + rank1Word(bv.words[offset], index % BV_WORD_BIT_SIZE)));
        return rank1_res + rank1Word(bv.words[offset], index % BV_WORD_BIT_SIZE);
    }

uint getFlatRankEntriesHuffman(uint bit_vector_size) {
    return bit_vector_size / BV_L1_BIT_SIZE + 1u;
}

uint wmh_getLevelStart(uint level, const uint level_starts_1_to_4[4]) {
    level--; // Force overflow for level 0 (uint). Will be optimized away for any getLevelStart(level+1) call.
    // For L0, 0 is correct. For L5 (complete bit vector size), may return any value as it is never used.
    return level < 4 ? level_starts_1_to_4[level] : 0u;
}

uint wm_huffman_access(uint position, const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::access()

    // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
    // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
    for (uint level = 0; level < HWM_LEVELS; level++) {
        if (_bv_access(position, bit_vector) != 0u) {
            assert(position != 0u || level == 4u, "first operation in stream must be 4u (PALETTE_ADV).");
            return level;
        } else {
            // TODO: we should not use the inverted CHC but the normal CHC, interpret 1 as left and 0 as right
            //  in the wavelet matrix to optimize the rank0 / rank1 queries
            const uint ones_before = _fr_rank1(position, bit_vector, wm_header) - wm_header.header.ones_before_level[level];
            const uint zeros_before = (position - wmh_getLevelStart(level, wm_header.header.level_starts_1_to_4)) - ones_before;
            position = wmh_getLevelStart(level + 1, wm_header.header.level_starts_1_to_4) + zeros_before;
        }
    }
    return HWM_LEVELS;
}


uint wm_huffman_rank(uint position, uint symbol, const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::rank()

    uint interval_start = 0;
    uvec2 chc = SYMBOL2CHC[symbol];
    uint bit_mask = 1u << (CHC_BIT_SIZE - 1);
    for (uint level = 0; (level < chc.x) && (position > 0); ++level) {
        const uint ones_before_interval = _fr_rank1(interval_start, bit_vector, wm_header);
        const uint ones_before_position = _fr_rank1(interval_start + position, bit_vector, wm_header) - ones_before_interval;
        // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
        // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
        if ((chc.y & bit_mask) != 0u) {
            return ones_before_position;
        } else {
            position = position - ones_before_position;
            // TODO: ones_before_level could become an uvec4 if we exclude this case for level == chc.length-1
            const uint ones_in_interval = ones_before_interval - wm_header.header.ones_before_level[level];
            interval_start = wmh_getLevelStart(level + 1, wm_header.header.level_starts_1_to_4)
                    + (interval_start - wmh_getLevelStart(level, wm_header.header.level_starts_1_to_4) - ones_in_interval);
        }
        bit_mask >>= 1;
    }
    return position;
}

// SERIAL ENCODING -----------------------------------------------------------------------------------------------------
#ifndef RANDOM_ACCESS

/** Decompresses the brick from the encoding array to the cache region at decoded_brick_start_uint up to the given
 * inverse LoD.
 * If start_at_inv_lod == 0, it is assumed that the output brick cache is set to INVALID at all entries.
 * If start_at_inv_lod > 0, it is assumed that the output brick cache is fully decoded up to (start_at_inv_lod-1).
 * Start_at_inv_Lod must not be the finest possible LoD. */
void decompressCSGVBrick(const uint brick_idx,
                         const uvec3 valid_brick_size, const uint start_at_inv_lod, const uint target_inv_lod,
                         const uint decoded_brick_start_uint) {
    fillCSGVBrick(decoded_brick_start_uint, target_inv_lod, INVALID);
    assert(false, "serial decoding not yet implemented for wavelet matrix");
}

#endif // no RANDOM_ACCESS
// RANDOM ACCESS DECODING ----------------------------------------------------------------------------------------------
#ifdef RANDOM_ACCESS


/** Decode a single voxel with index output_i in the target_inv_lod. Decoding is performed by chasing the operation
 * references from the output voxel to a palette reference. It is assumed that the brick encoding is located in a shared
 * memory buffer uint CSGV_SHARED_MEMORY_BRICK_ENCODING[]. */
void decompressCSGVVoxelSharedMemory(const uint output_i, const uint brick_encoding_length,
                                     const uvec3 valid_brick_size, const uint target_inv_lod,
                                     const uint decoded_brick_start_uint,
                                     const EncodingRef brick_encoding,
                                     const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint inv_lod_op_i = output_i;
    // corresponding voxel position within the inv. LoD
    uvec3 inv_lod_voxel = _cache_idx2pos(inv_lod_op_i);

    // obtain encoding operation read index (4 bit)
    assert(brick_encoding.buf[0] == 0u, "First operation in the opstrem must have start index 0.");
    uint enc_operation_index = brick_encoding.buf[inv_lod] + inv_lod_op_i;
    uint operation = wm_huffman_access(enc_operation_index, wm_header, bit_vector);

    // ToDo: handle stop bits
    assert((operation & STOP_BIT) == 0u, "stop bit not yet supported with random access");

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        uint operation_lsb = operation & 7u; // extract least significant 3 bits

        // equal to (operation_lsb != PALETTE_LAST && operation_lsb != PALETTE_ADV && operation_lsb != PALETTE_D)
        while (operation_lsb < 4u) {
            // find the read position for the next operation along the chain
            if (operation_lsb == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
                assert(inv_lod <= target_inv_lod, "LOD chasing overflow for Huffman Wavelet Matrix decoding.");
                inv_lod_op_i /= 8u;
                inv_lod_voxel = _cache_idx2pos(inv_lod_op_i);
            }
            // operation_lsb is NEIGHBOR_X, NEIGHBOR_Y, or NEIGHBOR_Z:
            else {
                // read from a neighbor in the next iteration
                const uint neighbor_index = operation_lsb - NEIGHBOR_X; // X: 0, Y: 1, Z: 2
                const uint child_index = inv_lod_op_i % 8u;

                inv_lod_voxel += neighbor[child_index][neighbor_index];
                inv_lod_op_i = _cache_pos2idx(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                    inv_lod_voxel = _cache_idx2pos(inv_lod_op_i);
                }
            }

            assert(enc_operation_index < wm_header.header.level_starts_1_to_4[0], "brick encoding out of bounds read");

            // at this point: inv_lod, inv_lod_op_i, and inv_lod_voxel must be valid and set correctly!
            enc_operation_index = brick_encoding.buf[inv_lod] + inv_lod_op_i;
            operation_lsb = wm_huffman_access(enc_operation_index, wm_header, bit_vector) & 7u;
        }

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint palette_index = wm_huffman_rank(enc_operation_index, PALETTE_ADV, wm_header, bit_vector);
        // the actual palette index may be offset depending on the operation
        if (operation_lsb == PALETTE_LAST) {
            palette_index--;
        }
        assertf(palette_index < brick_encoding.buf[PALETTE_SIZE_HEADER_INDEX], "palette index out of palette bounds, is (index, operation) %v2u", uvec2(palette_index, operation_lsb));
        assert(operation_lsb != PALETTE_D, "palette delta operation not supported with random access");

        // Write to the index in the output array. The output array's positions are in Morton order.
#ifdef PALETTE_CACHE
        // TODO: This is a race condition! Different threads write to (different bits of) the same uint in the cache
        writeEntryToCache(decoded_brick_start_uint, output_i, palette_index + 1u);
#else
        writeEntryToCache(decoded_brick_start_uint, output_i, brick_encoding.buf[brick_encoding_length - 1u - palette_index]);
#endif
    }
}

#endif // RANDOM_ACCESS

// DEBUGGING AND STATISTICS --------------------------------------------------------------------------------------------

void outputOperationStream(const uint brick_idx) {
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    WMHBrickHeaderRef wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
    BitVectorRef bit_vector = getWMHBitVectorFromEncoding(brick_encoding);

    const uint offset = 100;
    debugPrintfEXT("op-stream %u:  %v4u %v4u %v4u %v4u", brick_idx,
                    uvec4(wm_huffman_access(offset + 0, wm_header, bit_vector),
                          wm_huffman_access(offset + 1, wm_header, bit_vector),
                          wm_huffman_access(offset + 2, wm_header, bit_vector),
                          wm_huffman_access(offset + 3, wm_header, bit_vector)),
                    uvec4(wm_huffman_access(offset + 4, wm_header, bit_vector),
                          wm_huffman_access(offset + 5, wm_header, bit_vector),
                          wm_huffman_access(offset + 6, wm_header, bit_vector),
                          wm_huffman_access(offset + 7, wm_header, bit_vector)),
                    uvec4(wm_huffman_access(offset + 8, wm_header, bit_vector),
                          wm_huffman_access(offset + 9, wm_header, bit_vector),
                          wm_huffman_access(offset + 10, wm_header, bit_vector),
                          wm_huffman_access(offset + 11, wm_header, bit_vector)),
                    uvec4(wm_huffman_access(offset + 12, wm_header, bit_vector),
                          wm_huffman_access(offset + 13, wm_header, bit_vector),
                          wm_huffman_access(offset + 14, wm_header, bit_vector),
                          wm_huffman_access(offset + 15, wm_header, bit_vector)));
}

bool verifyBrickCompression(const uint brick_idx) {

    // Obtain a reference to the uint buffer containing this bricks encoding.
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);
    const uint base_header_size = LOD_COUNT * 2 + 1u;
    const uint total_header_size_one_fr = base_header_size + 12; // base header + WMHBrickHeader incl 1 FlatRank
    const uint header_start_lods = LOD_COUNT;

#if (BRICK_SIZE == 16)
    const uint total_voxels_in_brick = 4681;
#elif (BRICK_SIZE == 32)
    const uint total_voxels_in_brick = 37449;
#elif (BRICK_SIZE == 64)
    const uint total_voxels_in_brick = 299593;
#endif

    if (SIZEOF(WMHBrickHeaderRef) != 48) {
        debugPrintfEXT("WMHBrickHeader size must be 48 but is %u", uint(SIZEOF(WMHBrickHeaderRef)));
        return false;
    }

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < base_header_size + 1u + 1u) {
        debugPrintfEXT("brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette) = %u but is %u", base_header_size + 2u, brick_encoding_length);
        return false;
    }

    // check first header entry being base_header_size * 8
    if(brick_encoding.buf[0] != 0) {
        debugPrintfEXT("First encoding operation index must be 0.");
        return false;
    }

    // check palette start of first LoD being 0 and second LoD being 1
    if(brick_encoding.buf[header_start_lods] != 0u) {
        debugPrintfEXT("First palette start must be 0 but is %u", brick_encoding.buf[header_start_lods]);
        return false;
    }
    if(brick_encoding.buf[header_start_lods + 1u] != 1u) {
        debugPrintfEXT("Second palette start must be 1 but is %u", brick_encoding.buf[header_start_lods + 1u]);
        return false;
    }

    WMHBrickHeaderRef wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
    // maximum text size: HWM_LEVELS bits per voxel (i.e. 5 bit vectors with length of voxels in brick)
    if (wm_header.header.bit_vector_size == 0u || wm_header.header.bit_vector_size > total_voxels_in_brick * HWM_LEVELS) {
        debugPrintfEXT("Bit vector size must be within (0, %u) but is %u", total_voxels_in_brick * HWM_LEVELS, wm_header.header.bit_vector_size);
        return false;
    }
    if (getL1Entry(wm_header.header.fr[0]) != 0) {
        debugPrintfEXT("First flat rank L1 entry must be 0 but is %u", getL1Entry(wm_header.header.fr[0]));
        return false;
    }
    if (getL2Entry(wm_header.header.fr[0], 0) != 0) {
        debugPrintfEXT("First flat rank L1 entry must be 0 but is %u", getL1Entry(wm_header.header.fr[0]));
        return false;
    }
    if (wm_header.header.ones_before_level[0] != 0u) {
        debugPrintfEXT("First ones_before_level entry must be 0 but is %u", wm_header.header.ones_before_level[0]);
        return false;
    }
    if (wm_header.header.level_starts_1_to_4[0] > total_voxels_in_brick) {
        debugPrintfEXT("level_starts_1_to_4[0] must be the text size, limited by voxel count, but is %u", wm_header.header.level_starts_1_to_4[0]);
        return false;
    }

    BitVectorRef bit_vector = getWMHBitVectorFromEncoding(brick_encoding);
    if (wm_huffman_access(0, wm_header, bit_vector) != PALETTE_ADV) {
        debugPrintfEXT("First operation must be PALETTE_ADV but is %u", wm_huffman_access(0, wm_header, bit_vector));
        return false;
    }

    return true;
}


#endif // HUFFMAN_WM_DECODER_GLSL
