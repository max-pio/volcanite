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
// Parts of this code are based on the pasta-toolkit Wavelet Tree implementation by Florian Kurpicz which is licensed
// under the GPLv3 license. https://github.com/pasta-toolbox/wavelet_tree

#ifndef HUFFMAN_WM_DECODER_GLSL
#define HUFFMAN_WM_DECODER_GLSL

#include "cpp_glsl_include/csgv_constants.incl"
#include "volcanite/compression/csgv_utils.glsl"

/// This Decoder corresponds to the huffman-shaped wavelet matrix encoder from
/// ../../../../include/volcanite/compression/encoder/WaveletMatrixDecoder.hpp
/// and
/// ../../../../include/volcanite/compression/pack_wavelet_matrix.hpp
/// Supported configuration compile time defines:
///   RANDOM_ACCESS

#if ENCODING_MODE != HUFFMAN_WM_ENC
    #error "expected HUFFMAN_WM_ENC encoding mode"
#endif

#ifdef SEPARATE_DETAIL
    #error "wavelet matrix does not support detail separation"
#endif

#ifdef PALETTE_CACHE
    #error "wavelet matrix does not support palletized cache"
#endif

#ifndef RANDOM_ACCESS
    #error "wavelet matrix only supports random access"
#endif

#if !defined(RANDOM_ACCESS) && defined(DECODE_FROM_SHARED_MEMORY)
    #error "DECODE_FROM_SHARED_MEMORY can only be used with RANDOM_ACCESS"
#endif

// TYPE DEFINITIONS AND TYPE ACCESS ------------------------------------------------------------------------------------

// TODO: move bit vector implementations and macros to bit_vector.glsl

#ifndef SHARED_BIT_VECTOR
    #define SHARED_BIT_VECTOR s_bit_vector
#endif
#ifndef SHARED_WM_HEADER
    #define SHARED_WM_HEADER s_wm_header[0]
#endif
#ifndef SHARED_STOP_BITS_REF
    #define SHARED_STOP_BITS_REF s_stop_bits_ref
#endif

#include "volcanite/compression/decoder/HuffmanWMDecoder_types.glsl"

#include "volcanite/bit_vector.glsl"

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

WMHBrickHeaderRef getWMHBrickHeaderFromEncoding(EncodingRef brick_encoding) {
    return WMHBrickHeaderRef(bufferAddressAdd(uvec2(brick_encoding), WM_HEADER_INDEX));
}

BitVectorRef getWMHBitVectorFromEncoding(EncodingRef brick_encoding) {
    // the bit vector follows after the [LOD starts] [WMH Header] and its flexible array member [Flat Rank]
    // the flat rank size depends on the text size, which is the first element of the WM header at WM_HEADER_INDEX
    return BitVectorRef(bufferAddressAdd(uvec2(brick_encoding),
                                         WM_HEADER_INDEX + 10
                                         + UINT_PER_L12 * (brick_encoding.buf[WM_HEADER_INDEX] / BV_L1_BIT_SIZE + 1u)));
}

// UTILITY FUNCTIONS ---------------------------------------------------------------------------------------------------

// depending on if DECODE_FROM_SHARED_MEMORY is set:
//  the bit vector and wm_header are read from shared memory arrays, or
//  passed along as function parameters
// the following macros allow to implement both cases in the same file:

#ifdef DECODE_FROM_SHARED_MEMORY
    #define WM_BIT_VECTOR SHARED_BIT_VECTOR
    #define WM_HEADER SHARED_WM_HEADER
    #define FR_RANK1(index) _fr_rank1_wmh(index)
    #define WM_HUFFMAN_ACCESS(position) _wm_huffman_access(position)
    #define WM_HUFFMAN_RANK_PALETTE(position) _wm_huffman_rank_palette(position)
#else
    #define WM_BIT_VECTOR bit_vector.words
    #define WM_HEADER wm_header
    #define FR_RANK1(index) _fr_rank1_wmh(wm_header, bit_vector, index)
    #define WM_HUFFMAN_ACCESS(position) _wm_huffman_access(wm_header, bit_vector, position)
    #define WM_HUFFMAN_RANK_PALETTE(position) _wm_huffman_rank_palette(wm_header, bit_vector, position)
#endif

uint  getFlatRankEntries(uint bit_vector_size) {
    return bit_vector_size / BV_L1_BIT_SIZE + 1u;
}

uint wmh_getLevelStart(uint level, const uint level_starts_1_to_4[4]) {
    level--; // Force overflow for level 0 (uint). Will be optimized away for any getLevelStart(level+1) call.
    // For L0, 0 is correct. For L5 (complete bit vector size), may return any value as it is never used.
    return level < 4 ? level_starts_1_to_4[level] : 0u;
}

/// The wavelet matrix encoded brick header stores the flat rank at its end directly. This is a special
/// flat rank function that does not take a reference to the flat rank array but uses this header array instead.
/// Additionally, if DECODE_FROM_SHARED_MEMORY is defined, the bit vector and flat rank are not taken as arguments
/// but read from the shared memory arrays defined by WM_BIT_VECTOR and WM_HEADER instead.
uint _fr_rank1_wmh(
               #ifndef DECODE_FROM_SHARED_MEMORY
               const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector,
               #endif
               uint index) {
//#if 1
//        uint count = 0u;
//        const uint words = index / BV_WORD_BIT_SIZE;
//        for (uint i = 0; i < index / BV_WORD_BIT_SIZE; i++) {
//            count += bitCount64(WM_BIT_VECTOR[i]);
//        }
//        for (uint i = words * BV_WORD_BIT_SIZE; i < index; i++) {
//            if (BV_ACCESS(WM_BIT_VECTOR, i) == 1u)
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
    BV_WORD_TYPE l12 = WM_HEADER.fr[index / BV_L1_BIT_SIZE];
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
            rank1_res += bitCount64(WM_BIT_VECTOR[offset]);
            offset++;
        }
    #endif
    // if this is a rank(text_size) query, the inlining of the function lead to the potential out of bounds
    // access bv[offset] being ignored.

    assertf(rank1_res + WORD_RANK1(WM_BIT_VECTOR[offset], index % BV_WORD_BIT_SIZE) < (index == 0u ? 1u : index),
            "FR_RANK1 return value too high. [index, rank1]: [%v2u]",
            uvec2(index, rank1_res + WORD_RANK1(WM_BIT_VECTOR[offset], index % BV_WORD_BIT_SIZE)));
    return rank1_res + WORD_RANK1(WM_BIT_VECTOR[offset], index % BV_WORD_BIT_SIZE);
}

/// returns two addresses: the stop bit vector reference as (x, y) and its flat rank as (z, w). 
uvec4 getWMHStopBitsFromEncoding(const EncodingRef brick_encoding,
                                 const uint brick_encoding_length,
                                 const uint palette_size) {
    // layout within the brick encoding:
    // [...]  [stop bit flat rank] [stop bit vector] [1x uint32 stop bit vector uint32 element count] [palette]

    const uint stop_bv_length_lookup_index = brick_encoding_length - palette_size - 1u;
    const uint stop_bv_length = brick_encoding.buf[stop_bv_length_lookup_index];
    assert(palette_size + 1u + stop_bv_length < brick_encoding_length, "stop_bv_length exceeds brick encoding length");

    const uvec2 bv_ref = bufferAddressAdd(uvec2(brick_encoding), stop_bv_length_lookup_index - stop_bv_length);
    // note regarding the (BV_WORD_BIT_SIZE / 32u): getFlatRankEntriesHuffman counts in 64 bit elements, but the
    // alignment of BitVectorRef is 4 byte (32 bit). For that reason, pointer arithmetic on these adresses must use
    // 32 bit indices. The conversion factor from 64 bit to 32 bit index offsets is (BV_WORD_BIT_SIZE / 32u).
    const uvec2 fr_ref = bufferAddressSub(bv_ref,  getFlatRankEntries(stop_bv_length * 32u) * (BV_WORD_BIT_SIZE / 32u));

    assertf(_get_L1_entry(BitVectorRef(fr_ref).words[0]) == 0u, "corrupted flat rank: first L1 is not 0 but %u",
            _get_L1_entry(BitVectorRef(fr_ref).words[0]));

    return uvec4(bv_ref, fr_ref);
}

/// Handles encoding index and operation index changes from stop bits:
/// Inplace assign of inv_lod and inv_lod_op_i to the first parent of inv_lod_op_i that exists in the encoding stream.
/// Compute encoding index mapping as inv_lod_starts[inv_lod] + inv_lod_op_i - offset_from_stop_bits.
/// Returns the index in the encoding arrays from where to read the corresponding operation.
uint getEncodingIndexWithStopBits(inout uint inv_lod, inout uint inv_lod_op_i, const EncodingRef inv_lod_starts,
                                  const uvec4 stop_bits) {

    assert(inv_lod_starts.buf[0] == 0u, "inv_lod_starts must start with 0");

    uint offset = 0u;
    uint covered_nodes_shift = 3u * inv_lod;

    BitVectorRef stop_bits_bv = BitVectorRef(stop_bits.xy);
    BitVectorRef stop_bits_fr = BitVectorRef(stop_bits.zw);
    assertf(_get_L1_entry(stop_bits_fr.words[0]) == 0u, "neg.s.b.o. first L1 is not 0 but %u", _get_L1_entry(stop_bits_fr.words[0]));

    for (uint l = 0; l < inv_lod; l++) {
        // encoding index of the parent node within its inverse LOD.
        // each parent node covers 2³ nodes in the next level, (2³)³ nodes in the next level afterwards etc.
        const uint parent_op_i = inv_lod_op_i >> covered_nodes_shift;

        // if the parent sets a stop bit: set inv_lod and inv_lod_op_i so that they update
        if (BV_ACCESS(stop_bits_bv.words, inv_lod_starts.buf[l] + parent_op_i - offset) != 0u) {
            inv_lod = l;
            inv_lod_op_i = parent_op_i;
            break;
        }

        // TODO: can the second _fr_rank1 be computed iteratively or cancelled out?
        // add offset introduced by the nodes in this LOD
        offset += _fr_rank1(inv_lod_starts.buf[l] + parent_op_i - offset, stop_bits_bv, stop_bits_fr)
                  - _fr_rank1(inv_lod_starts.buf[l], stop_bits_bv, stop_bits_fr);

        // in the next finer LOD, the (grand-)parent grid node covers only 1/8th of the nodes in inv_lod compared
        // to the parent from the coarser level
        covered_nodes_shift -= 3u;
        offset *= 8u;
    }

    assertf(offset <= inv_lod_op_i, "stop bit offset too large (offset, index): %v2u", uvec2(offset, inv_lod_op_i));
    return inv_lod_starts.buf[inv_lod] + inv_lod_op_i - offset;
}

uint _wm_huffman_access(
                        #ifndef DECODE_FROM_SHARED_MEMORY
                        const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector,
                        #endif
                        uint position) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::access()

    // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
    // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
    for (uint level = 0; level < HWM_LEVELS; level++) {
        // debugPrintfEXT("||| level %u ||| bv_access(WM_BIT_VECTOR, %u)=%u fr_rank1=%u, ones_before_level=%u", level, position, BV_ACCESS(WM_BIT_VECTOR, position), FR_RANK1(position), WM_HEADER.ones_before_level[level]);

        assert(position < WM_HEADER.bit_vector_size, "reading bit vector index out of bounds.");
        if (BV_ACCESS(WM_BIT_VECTOR, position) != 0u) {
            assertf(position != 0u || level == 4u, "first operation in stream must be 4u (PALETTE_ADV) but (pos, op) is (%v2u)", uvec2(position, HWM_LEVELS));
            return level;
        } else {
            assert(position >= wmh_getLevelStart(level, WM_HEADER.level_starts_1_to_4), "position outside of level");
            assert(FR_RANK1(position) >= WM_HEADER.ones_before_level[level], "rank1 must yield at least as many rank1 entries as there are before the level");
            // TODO: we should not use the inverted CHC but the normal CHC, interpret 1 as left and 0 as right
            //  in the wavelet matrix to optimize the rank0 / rank1 queries
            const uint ones_before = FR_RANK1(position) - WM_HEADER.ones_before_level[level];
            const uint zeros_before = (position - wmh_getLevelStart(level, WM_HEADER.level_starts_1_to_4)) - ones_before;
            position = wmh_getLevelStart(level + 1, WM_HEADER.level_starts_1_to_4) + zeros_before;
        }
    }
    return HWM_LEVELS;
}


uint _wm_huffman_rank_palette(
                  #ifndef DECODE_FROM_SHARED_MEMORY
                      const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector,
                  #endif
                      uint position) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::rank()

    // the PALETTE_ADV operation consists of 5 bits (00001) => 4 loop iterations for the internal zeros

    //uint interval_start = 0;
    #pragma unroll
    for (uint level = 0; level < 4 && (position > 0); ++level) {
        // this would be the general 0 branch of a wavelet tree. Given that our trees have no 1 childs, the interval
        // starts are always exactly the level starts => ones_before_interval == WM_HEADER.ones_before_level[level];
        //    const uint ones_before_interval = FR_RANK1(interval_start);
        //    const uint ones_before_position = FR_RANK1(interval_start + position) - ones_before_interval;
        //
        //    position = position - ones_before_position;
        //    const uint ones_in_interval = ones_before_interval - WM_HEADER.ones_before_level[level];
        //    interval_start = wmh_getLevelStart(level + 1, WM_HEADER.level_starts_1_to_4)
        //            + (interval_start - wmh_getLevelStart(level, WM_HEADER.level_starts_1_to_4) - ones_in_interval);

        const uint interval_start = wmh_getLevelStart(level, WM_HEADER.level_starts_1_to_4);
        const uint ones_before_position = FR_RANK1(interval_start + position) - WM_HEADER.ones_before_level[level];
        position = position - ones_before_position;
    }

    const uint ones_before_interval = WM_HEADER.ones_before_level[4u];
    const uint interval_start = wmh_getLevelStart(4u, WM_HEADER.level_starts_1_to_4);
    const uint ones_before_position = FR_RANK1(interval_start + position) - ones_before_interval;
    return ones_before_position;
}

uint _wm_huffman_rank(
                  #ifndef DECODE_FROM_SHARED_MEMORY
                      const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector,
                  #endif
                      uint position, uint symbol) {
    // see: volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp HuffmanWaveletMatrix::rank()

    uint interval_start = 0;
    uvec2 chc = SYMBOL2CHC[symbol];
    uint bit_mask = 1u << (CHC_BIT_SIZE - 1);
    for (uint level = 0; (level < chc.x) && (position > 0); ++level) {
        const uint ones_before_interval = FR_RANK1(interval_start);
        const uint ones_before_position = FR_RANK1(interval_start + position) - ones_before_interval;
        // due to the assumptions for the canonical Huffman codes used in the wavelet matrix,
        // ANY 1 bit directly terminates the canonical huffman code and the symbol is the position of this bit.
        if ((chc.y & bit_mask) != 0u) {
            return ones_before_position;
        } else {
            position = position - ones_before_position;
            // TODO: ones_before_level could become an uvec4 if we exclude this case for level == chc.length-1
            const uint ones_in_interval = ones_before_interval - WM_HEADER.ones_before_level[level];
            interval_start = wmh_getLevelStart(level + 1, WM_HEADER.level_starts_1_to_4)
                    + (interval_start - wmh_getLevelStart(level, WM_HEADER.level_starts_1_to_4) - ones_in_interval);
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

/// Returns the palette index that stores the label of the voxel at location output_i within the target_inv_lod.
uint getPaletteIndexOfCSGVVoxel(const uint output_i, const uint target_inv_lod,
                                const EncodingRef brick_encoding, const uint brick_encoding_length
                            #ifndef DECODE_FROM_SHARED_MEMORY
                                , const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector
                            #endif
                            #if (OP_MASK & OP_STOP_BIT)
                                , const uvec4 stop_bits
                            #endif
                                ) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint inv_lod_op_i = output_i;

    // obtain encoding operation read index (4 bit)
    assert(brick_encoding.buf[0] == 0u, "First operation in the opstrem must have start index 0.");
#if (OP_MASK & OP_STOP_BIT)
    // getNegativeStopBitOffset may move inv_lod and inv_lod_op_i to a coarser LOD
    uint enc_operation_index = getEncodingIndexWithStopBits(inv_lod, inv_lod_op_i, brick_encoding, stop_bits);
#else
    uint enc_operation_index = brick_encoding.buf[inv_lod] + inv_lod_op_i;
#endif

    assertf(inv_lod < LOD_COUNT, "inv lod out of bounds %u", inv_lod);
    assertf(enc_operation_index < WM_HEADER.level_starts_1_to_4[0], "brick encoding out of bounds read (access, bound, diff): %v3u", uvec3(enc_operation_index, WM_HEADER.level_starts_1_to_4[0], enc_operation_index - WM_HEADER.level_starts_1_to_4[0]));
    uint operation = WM_HUFFMAN_ACCESS(enc_operation_index);

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {

        // equal to (operation != PALETTE_LAST && operation != PALETTE_ADV && operation != PALETTE_D)
        while (operation < 4u) {
            // find the read position for the next operation along the chain
            if (operation == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
                inv_lod_op_i /= 8u;
                assert(inv_lod <= target_inv_lod, "LOD chasing overflow for Huffman Wavelet Matrix decoding.");
            }
            // operation is NEIGHBOR_X, NEIGHBOR_Y, or NEIGHBOR_Z:
            else {
                // read from a neighbor in the next iteration
                const uint neighbor_index = operation - NEIGHBOR_X; // X: 0, Y: 1, Z: 2
                const uint child_index = inv_lod_op_i % 8u;

                const uvec3 inv_lod_voxel = uvec3(ivec3(_cache_idx2pos(inv_lod_op_i)) + neighbor[child_index][neighbor_index]);
                inv_lod_op_i = _cache_pos2idx(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                }
            }

            #if (OP_MASK & OP_STOP_BIT)
                enc_operation_index = getEncodingIndexWithStopBits(inv_lod, inv_lod_op_i, brick_encoding, stop_bits);
            #else
                enc_operation_index = brick_encoding.buf[inv_lod] + inv_lod_op_i;
            #endif

            // at this point: inv_lod, and inv_lod_op_i must be valid and set correctly
            assertf(inv_lod_op_i < BRICK_SIZE * BRICK_SIZE * BRICK_SIZE, "inv_lod_op_i out of (brick size)^3 bounds: %u", output_i);
            assert(inv_lod <= target_inv_lod, "LOD chasing overflow for Huffman Wavelet Matrix decoding.");
            assertf(enc_operation_index < WM_HEADER.level_starts_1_to_4[0], "brick encoding out of bounds read (access, bound, inv_lod, inv_lod_op_i): %v4u", uvec4(enc_operation_index, WM_HEADER.level_starts_1_to_4[0], inv_lod, inv_lod_op_i));

            operation = WM_HUFFMAN_ACCESS(enc_operation_index);
            assertf(enc_operation_index != 0u || operation == PALETTE_ADV, "first brick operation must be PALETTE_ADV but is %u", operation);
        }
        assert((operation & STOP_BIT) == 0u, "Wavelet Matrix encoding does not support stop bits encoded in OP stream");

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint palette_index = WM_HUFFMAN_RANK_PALETTE(enc_operation_index);
        // the actual palette index may be offset depending on the operation
        if (operation == PALETTE_LAST) {
            palette_index--;
        }
        assertf(palette_index < brick_encoding.buf[PALETTE_SIZE_HEADER_INDEX], "palette index out of palette bounds, is (index, operation) %v2u", uvec2(palette_index, operation));

        return palette_index;
    }
}

#if CACHE_MODE == CACHE_BRICKS

/// Decode a single voxel with index output_i in the target_inv_lod. Decoding is performed by chasing the operation
/// references from the output voxel to a palette reference.
/// If DECODE_FROM_SHARED_MEMORY is set, it is assumed that the WMHBrickHeader an bit vector are
/// present in shared memory as SHARED_WM_HEADER and SHARED_BIT_VECTOR.
/// Otherwise, they are passed as function arguments. */
void decompressCSGVVoxelToCache(const uint output_i, const uint target_inv_lod, const uvec3 valid_brick_size,
                                const EncodingRef brick_encoding, const uint brick_encoding_length,
                            #ifndef DECODE_FROM_SHARED_MEMORY
                                const WMHBrickHeaderRef wm_header, const BitVectorRef bit_vector,
                            #endif
                                const uint decoded_brick_start_uint) {

    uint palette_index = getPaletteIndexOfCSGVVoxel(output_i, target_inv_lod,
                                                    brick_encoding, brick_encoding_length
                                                #ifndef DECODE_FROM_SHARED_MEMORY
                                                    , wm_header, bit_vector
                                                #endif
    // Note: stop bits could be moved to shared memory as well for DECODE_FORM_SHARED_MEMORY, as with WM_HEADER
                                                #if (OP_MASK & OP_STOP_BIT)
                                                    #ifndef DECODE_FROM_SHARED_MEMORY
                                                        , getWMHStopBitsFromEncoding(brick_encoding,
                                                                                    brick_encoding_length,
                                                                                    brick_encoding.buf[PALETTE_SIZE_HEADER_INDEX])
                                                    #else
                                                        , SHARED_STOP_BITS_REF
                                                    #endif
                                                #endif
                                                    );

    // Write to the index in the output array. The output array's positions are in Morton order.
#ifdef PALETTE_CACHE
    // TODO: This is a race condition! Different threads write to (different bits of) the same uint in the cache
    writeEntryToCache(decoded_brick_start_uint, output_i, palette_index + 1u);
#else
    writeEntryToCache(decoded_brick_start_uint, output_i, brick_encoding.buf[brick_encoding_length - 1u - palette_index]);
#endif
}

#endif // CACHE_MODE == CACHE_BRICKS

#ifndef DECODE_FROM_SHARED_MEMORY
uint decompressCSGVVoxel(const uint brick_idx, const uvec3 brick_voxel, const uint target_inv_lod) {
    assertf(brick_idx < g_brick_idx_count, "brick idx out of bounds (is, bound) %v2u", uvec2(brick_idx, g_brick_idx_count));
    assertf(all(lessThan(brick_voxel, uvec3(BRICK_SIZE))), "brick voxel out of brick size bounds, is: %v3u", brick_voxel);

    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);
    WMHBrickHeaderRef wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
    BitVectorRef bit_vector = getWMHBitVectorFromEncoding(brick_encoding);

     const uint lod_width = BRICK_SIZE >> target_inv_lod;
     const uint voxel_idx = _cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width);
    // same as:
    // const uint voxel_idx = _cache_pos2idx(brick_voxel) / (1u << (3 * (findMSB(BRICK_SIZE) - target_inv_lod)));

    uint palette_index = getPaletteIndexOfCSGVVoxel(voxel_idx, target_inv_lod,
                                                    brick_encoding, brick_encoding_length,
                                                    wm_header, bit_vector
                                                #if (OP_MASK & OP_STOP_BIT)
                                                    , getWMHStopBitsFromEncoding(brick_encoding,
                                                                                 brick_encoding_length,
                                                                                 brick_encoding.buf[PALETTE_SIZE_HEADER_INDEX])
                                                #endif
                                                    );
    assertf(palette_index < getBrickPaletteLength(brick_idx),
           "palette index out of palette bounds (brick, palette_idx, palette_length): %v3u",
           uvec3(brick_idx, palette_index, getBrickPaletteLength(brick_idx)));
    return brick_encoding.buf[brick_encoding_length - 1u - palette_index];
}
#endif

#endif // RANDOM_ACCESS

// DEBUGGING AND STATISTICS --------------------------------------------------------------------------------------------

bool TEST_BIT_VECTOR() {
    BV_WORD_TYPE v = uint64_t(12751266098003836929ul);
    // 1011 0000 1111 0101 1001 1000 1111 0101:0000 0000 0000 0000 0000 0000 0000 0001
    //   60   56   52   48   44   40   36   32   28   24   20   16   12    8    4    0
    // bitCount = 19

    assertf(bitCount64(v) == 19, "wrong bitcount is %u", bitCount64(v));
    //
    assertf(WORD_RANK1(v, 0) == 0, "wrong WORD_RANK1 0 is %u", WORD_RANK1(v, 0));
    assertf(WORD_RANK1(v, 1) == 1, "wrong WORD_RANK1 1 is %u", WORD_RANK1(v, 1));
    assertf(WORD_RANK1(v, 32) == 1, "wrong WORD_RANK1 32 is %u", WORD_RANK1(v, 32));
    assertf(WORD_RANK1(v, 33) == 2, "wrong WORD_RANK1 33 is %u", WORD_RANK1(v, 33));
    assertf(WORD_RANK1(v, 63) == 18, "wrong WORD_RANK1 63 is %u", WORD_RANK1(v, 63));
    assertf(WORD_RANK1(v, 64) == 19, "wrong WORD_RANK1 64 is %u", WORD_RANK1(v, 64));
    //
    assertf(bitfieldExtract64(v, 0, 0) == 0, "wrong bitfieldExtract 0 0 is %u", bitfieldExtract64(v, 0, 0));
    assertf(bitfieldExtract64(v, 3, 30) == 536870912u, "wrong bitfieldExtract 3 30 is %u", bitfieldExtract64(v, 3, 30));
    assertf(bitfieldExtract64(v, 32, 13) == 6389, "wrong bitfieldExtract 32 13 is %u", bitfieldExtract64(v, 32, 13));
    assertf(bitfieldExtract64(v, 56, 8) == 176, "wrong bitfieldExtract 56 8 is %u", bitfieldExtract64(v, 56, 8));
    return true;
}


void outputOperationStream(const uint brick_idx, uint offset) {
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    WMHBrickHeaderRef wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
    BitVectorRef bit_vector = getWMHBitVectorFromEncoding(brick_encoding);

    debugPrintfEXT("op-stream %u:  %v4u, %v4u, %v4u, %v4u", brick_idx,
                    uvec4(WM_HUFFMAN_ACCESS(offset + 0),
                          WM_HUFFMAN_ACCESS(offset + 1),
                          WM_HUFFMAN_ACCESS(offset + 2),
                          WM_HUFFMAN_ACCESS(offset + 3)),
                    uvec4(WM_HUFFMAN_ACCESS(offset + 4),
                          WM_HUFFMAN_ACCESS(offset + 5),
                          WM_HUFFMAN_ACCESS(offset + 6),
                          WM_HUFFMAN_ACCESS(offset + 7)),
                    uvec4(WM_HUFFMAN_ACCESS(offset + 8),
                          WM_HUFFMAN_ACCESS(offset + 9),
                          WM_HUFFMAN_ACCESS(offset + 10),
                          WM_HUFFMAN_ACCESS(offset + 11)),
                    uvec4(WM_HUFFMAN_ACCESS(offset + 12),
                          WM_HUFFMAN_ACCESS(offset + 13),
                          WM_HUFFMAN_ACCESS(offset + 14),
                          WM_HUFFMAN_ACCESS(offset + 15)));

//    debugPrintfEXT("bit-vector start %u:  %v2u  %v2u  %v2u  %v2u", brick_idx, unpackUint2x32(bit_vector.words[0]),
//                   unpackUint2x32(bit_vector.words[1]), unpackUint2x32(bit_vector.words[2]), unpackUint2x32(bit_vector.words[3]));
//
//    for (uint i = 0; i <  getFlatRankEntries(wm_header.bit_vector_size); i++)
//        debugPrintfEXT("wmh fr start %u at [%u]:  %v2u", brick_idx, i, unpackUint2x32(wm_header.fr[i]));
//
//    debugPrintfEXT("header %u:  bit_vector_size %u, ones_before_level %v4u, %u,  level_starts_1_to_4 %v4u, fr[0] %v2u",
//                        brick_idx,
//                        wm_header.bit_vector_size,
//                        uvec4(wm_header.ones_before_level[0],
//                              wm_header.ones_before_level[1],
//                              wm_header.ones_before_level[2],
//                              wm_header.ones_before_level[3]),
//                        wm_header.ones_before_level[4],
//                        uvec4(wm_header.level_starts_1_to_4[0],
//                              wm_header.level_starts_1_to_4[1],
//                              wm_header.level_starts_1_to_4[2],
//                              wm_header.level_starts_1_to_4[3]),
//                        unpackUint2x32(wm_header.fr[0]));
}

bool verifyBrickCompression(const uint brick_idx) {

    // Obtain a reference to the uint buffer containing this bricks encoding.
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);
    const uint base_header_size = LOD_COUNT * 2 + 1u;
    const uint total_header_size_one_fr = base_header_size + 12; // base header + WMHBrickHeader incl 1 FlatRank
    const uint header_start_lods = LOD_COUNT;

#if (BRICK_SIZE == 8)
    const uint total_voxels_in_brick = 585;
#elif (BRICK_SIZE == 16)
    const uint total_voxels_in_brick = 4681;
#elif (BRICK_SIZE == 32)
    const uint total_voxels_in_brick = 37449;
#elif (BRICK_SIZE == 64)
    const uint total_voxels_in_brick = 299593;
#elif (BRICK_SIZE == 128)
    const uint total_voxels_in_brick = 2396745;
#endif

    if (SIZEOF(WMHBrickHeaderRef) != 48) {
        debugPrintfEXT("[brick %u] WMHBrickHeader size must be 48 but is %u", brick_idx, uint(SIZEOF(WMHBrickHeaderRef)));
        return false;
    }

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < base_header_size + 1u + 1u) {
        debugPrintfEXT("[brick %u] brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette) = %u but is %u", brick_idx, base_header_size + 2u, brick_encoding_length);
        return false;
    }

    // check first header entry being base_header_size * 8
    if(brick_encoding.buf[0] != 0) {
        debugPrintfEXT("[brick %u] First encoding operation index must be 0.", brick_idx);
        return false;
    }

    // Brick headers do no longer store LOD palette starts
//    // check palette start of first LoD being 0 and second LoD being 1
//    if(brick_encoding.buf[header_start_lods] != 0u) {
//        debugPrintfEXT("[brick %u] First palette start must be 0 but is %u", brick_idx, brick_encoding.buf[header_start_lods]);
//        return false;
//    }
//    if(brick_encoding.buf[header_start_lods + 1u] != 1u) {
//        debugPrintfEXT("[brick %u] Second palette start must be 1 but is %u", brick_idx, brick_encoding.buf[header_start_lods + 1u]);
//        return false;
//    }

    WMHBrickHeaderRef wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
    // maximum text size: HWM_LEVELS bits per voxel (i.e. 5 bit vectors with length of voxels in brick)
    if (wm_header.bit_vector_size == 0u || wm_header.bit_vector_size > total_voxels_in_brick * HWM_LEVELS) {
        debugPrintfEXT("[brick %u] Bit vector size must be within (0, %u) but is %u", brick_idx, total_voxels_in_brick * HWM_LEVELS, wm_header.bit_vector_size);
        return false;
    }
    if (_get_L1_entry(wm_header.fr[0]) != 0) {
        debugPrintfEXT("[brick %u] First flat rank L1 entry must be 0 but is %u", brick_idx, _get_L1_entry(wm_header.fr[0]));
        return false;
    }
    if (_get_L2_entry(wm_header.fr[0], 0) != 0) {
        debugPrintfEXT("[brick %u] First flat rank L1 entry must be 0 but is %u", brick_idx, _get_L1_entry(wm_header.fr[0]));
        return false;
    }
    if (wm_header.ones_before_level[0] != 0u) {
        debugPrintfEXT("[brick %u] First ones_before_level entry must be 0 but is %u", brick_idx, wm_header.ones_before_level[0]);
        return false;
    }
    if (wm_header.level_starts_1_to_4[0] > total_voxels_in_brick) {
        debugPrintfEXT("[brick %u] level_starts_1_to_4[0] must be the text size, limited by voxel count, but is %u", brick_idx, wm_header.level_starts_1_to_4[0]);
        return false;
    }

    uint flat_rank_entries =  getFlatRankEntries(wm_header.bit_vector_size);
    for (int i = 1; i < flat_rank_entries; i++) {
        if (_get_L1_entry(wm_header.fr[i]) < _get_L1_entry(wm_header.fr[i-1])) {
            debugPrintfEXT("[brick %u] Flat Rank L1 entries must be ascending but for two entries entries are %v2u", brick_idx, uvec2(_get_L1_entry(wm_header.fr[i-1]), _get_L1_entry(wm_header.fr[i])));
            return false;
        }
    }

#ifndef DECODE_FROM_SHARED_MEMORY
    BitVectorRef bit_vector = getWMHBitVectorFromEncoding(brick_encoding);
    if (_wm_huffman_access(wm_header, bit_vector, 0) != PALETTE_ADV) {
        debugPrintfEXT("[brick %u] First operation must be PALETTE_ADV (4) but is %u", brick_idx, _wm_huffman_access(wm_header, bit_vector, 0));
        return false;
    }
#endif

    return true;
}


#endif // HUFFMAN_WM_DECODER_GLSL
