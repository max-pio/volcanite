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

#ifndef NIBBLE_DECODER_GLSL
#define NIBBLE_DECODER_GLSL

#include "volcanite/compression/csgv_utils.glsl"

/// This Decoder corresponds to the encoding from ../../../../include/volcanite/compression/encoder/NibbleEncoder.hpp
/// Supported configuration compile time defines:
///   PALETTE_CACHE
///   SEPARATE_DETAIL
///   RANDOM_ACCESS
///   DECODE_FROM_SHARED_MEMORY

#if ENCODING_MODE != NIBBLE_ENC
    #error "expected NIBBLE_ENC encoding mode"
#endif

#if defined(RANDOM_ACCESS) && defined(SEPARATE_DETAIL)
    #error "RANDOM_ACCESS cannot be used with NIBBLE_ENC and DETAIL_SEPARATION"
#endif

#if !defined(RANDOM_ACCESS) && defined(DECODE_FROM_SHARED_MEMORY)
    #error "DECODE_FROM_SHARED_MEMORY can only be used with RANDOM_ACCESS"
#endif

#if defined(RANDOM_ACCESS) && (OP_MASK & OP_STOP_BIT)
    #error "OP_STOP_BIT cannot be used with NIBBLE_ENC and RANDOM_ACCESS"
#endif

// SERIAL ENCODING -----------------------------------------------------------------------------------------------------
#ifndef RANDOM_ACCESS

uint _unpack4BitFromEncoding(EncodingRef brick_start, uint entry_id) {
    return bitfieldExtract(brick_start.buf[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}

struct CSGVReadState {
    uint idxE;              ///< read position in brick encoding (counted in nibbles without rANS, in bytes with rANS)
};

uint _readNextLodOperationFromEncoding(EncodingRef brick_start, inout CSGVReadState state) {
    return _unpack4BitFromEncoding(brick_start, state.idxE++);
}

/** Decompresses the brick from the encoding array to the cache region at decoded_brick_start_uint up to the given
 * inverse LoD.
 * If start_at_inv_lod == 0, it is assumed that the output brick cache is set to INVALID at all entries.
 * If start_at_inv_lod > 0, it is assumed that the output brick cache is fully decoded up to (start_at_inv_lod-1).
 * Start_at_inv_Lod must not be the finest possible LoD. */
void decompressCSGVBrick(const uint brick_idx,
                         const uvec3 valid_brick_size, const uint start_at_inv_lod, const uint target_inv_lod,
                         const uint decoded_brick_start_uint) {

    // safe test: do not decompress anything, instead fill the voxels with dummy values.
//     fillCSGVBrick(decoded_brick_start_uint, target_inv_lod, (brick_idx / 7) % getBrickPaletteLength(brick_idx));
//     return;

    // the cache region must be prepared with resetCSGVBrick before decoding
#ifdef PALETTE_CACHE
    assertf(readEntryFromCache(decoded_brick_start_uint, 0u) == 0, "brick cache region at %u not reset before decoding", CSGV_DECODING_ARRAY[decoded_brick_start_uint]);
#else
    assertf(CSGV_DECODING_ARRAY[decoded_brick_start_uint] == INVALID, "brick cache region at %u not reset before decoding", decoded_brick_start_uint);
#endif

    // The starting position of the current LoD in the encoding array, measured in elements of entry_t. Taken from first brick header entries.
    uint local_lod_i;           // [0, 7] local index of element within the LoD block of the coarser parent element.
                                // Used for parent_value and neighbor-lookup index.

    // Obtain a reference to the uint buffer containing this bricks encoding.
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);
#ifndef PALETTE_CACHE
    EncodingRef brick_palette = brick_encoding;
#endif

    // the palette starts at the end of the encoding block
#ifdef PALETTE_CACHE
    uint paletteE = 1u;         // 0 is the magic number for unwritten output elements. 1 the first palette entry
#else
    uint paletteE = brick_encoding_length - 1u;
#endif
    CSGVReadState readState;

    readState.idxE = brick_encoding.buf[start_at_inv_lod];  // current read entry, in number of nibbles (4 bit)

    uint output_size = (1u << target_inv_lod); // voxel count in each output brick dim. for LoD. BRICK_SIZE on finest LoD.
    uint lod_width = (1u << target_inv_lod) / (1u << start_at_inv_lod);
    uint index_step = (lod_width * lod_width * lod_width);
    uint parent_value;

    // Brick encoding order goes from the coarsest inverse LoD (0) to the finest invese LoD (LOD_COUNT - 1).
    for(uint inv_lod = start_at_inv_lod; inv_lod <= target_inv_lod; inv_lod++) {
#ifdef SEPARATE_DETAIL
        if(inv_lod == LOD_COUNT - 1u) {
            brick_encoding = getBrickDetailEncodingRef(brick_idx);
            readState.idxE = 0u;
        }
#endif

        // Iterate over output elements in the brick's cache region in encoding order (Z-curve) within the current LoD
        for (uint i = 0u; i < output_size * output_size * output_size; i += index_step) {
            // If an LoD block is completely outside the volume (i.e. it's first element is not within the volume).
            // It is skipped as it won't have any entries in the encoding.
            if (any(greaterThanEqual(_cache_idx2pos(i).xyz * (BRICK_SIZE/output_size), valid_brick_size)))
                continue;

            // Entries in the current LoD span 2*2*2=8 elements of the coarser LoD above.
            // Every 8th element the new parent is fetched.
            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            local_lod_i = (i % (index_step*8))/index_step;
            if (inv_lod > 0u && i % (index_step*8) == 0) {
                // If the last element of this 2x2x2 block is set, the subtree is already filled completely.
                // This indicates that a STOP_BIT was set for this area and it can be skipped.
#ifdef PALETTE_CACHE
                if (readEntryFromCache(decoded_brick_start_uint, i + (index_step * 7)) != 0) {
#else
                if (readEntryFromCache(decoded_brick_start_uint, i + (index_step * 7)) != INVALID) {
#endif
                    i += (index_step*7);
                    continue;
                }

                parent_value = readEntryFromCache(decoded_brick_start_uint, i);
            }

            // Get the next operation from the brick encoding stream and apply it.
            uint operation = _readNextLodOperationFromEncoding(brick_encoding, readState);

            // Extract least significant 3 bits that store the operation.
            uint operation_lsb = operation & 7u;
            if (operation_lsb == PARENT) {
                writeEntryToCache(decoded_brick_start_uint, i, parent_value);
            }
            else if (operation_lsb <= NEIGHBOR_Z) {
                // Handle NEIGHBOR_X (1), NEIGHBOR_Y (2), and NEIGHBOR_Z (3) with another offset lookup.
                writeEntryToCache(decoded_brick_start_uint, i,
                                  _valueOfNeighborCache(_cache_idx2pos(i), local_lod_i, lod_width,
                                                   int(operation_lsb - NEIGHBOR_X), decoded_brick_start_uint));
            }
#ifdef PALETTE_CACHE
            // With a palettized cache, the *ascending* palette indices in the bricks *reverse* palette are stored.
            // An index of 1 references the first entry of the reverse palette, at the end of this brick's encoding.
            else if (operation_lsb == PALETTE_ADV) {
                writeEntryToCache(decoded_brick_start_uint, i, paletteE++);
            }
            else if (operation_lsb == PALETTE_LAST) {
                writeEntryToCache(decoded_brick_start_uint, i, paletteE - 1);
            }
            else if (operation_lsb == PALETTE_D) {
                #if ((OP_MASK & OP_USE_OLD_PAL_D_BIT) != 0)
                    const uint palette_delta = _readNextLodOperationFromEncoding(brick_encoding, readState);
                #else
                    uint palette_delta = 0u;
                    while (true) {
                        uint next_delta_bits = _readNextLodOperationFromEncoding(brick_encoding, readState);
                        // 3 LSB are the next three bits of delta
                        palette_delta = (palette_delta << 3u) | (next_delta_bits & 7u);
                        if ((next_delta_bits & 8u) == 0u)
                            break;
                    }
                #endif
                writeEntryToCache(decoded_brick_start_uint, i, paletteE - palette_delta - 2u);
            }
#else
            // When the cache is not palettized, 32 bit labels are directly stored in the cache.
            else if (operation_lsb == PALETTE_ADV) {
                writeEntryToCache(decoded_brick_start_uint, i, brick_palette.buf[paletteE--]);
            }
            else if (operation_lsb == PALETTE_LAST) {
                writeEntryToCache(decoded_brick_start_uint, i, brick_palette.buf[paletteE+1]);
            }
            else if (operation_lsb == PALETTE_D) {
                #if ((OP_MASK & OP_USE_OLD_PAL_D_BIT) != 0)
                    const uint palette_delta = _readNextLodOperationFromEncoding(brick_encoding, readState);
                #else
                    uint palette_delta = 0u;
                    while (true) {
                        uint next_delta_bits = _readNextLodOperationFromEncoding(brick_encoding, readState);
                        // 3 LSB are the next three bits of delta
                        palette_delta = (palette_delta << 3u) | (next_delta_bits & 7u);
                        if ((next_delta_bits & 8u) == 0u)
                            break;
                    }
                #endif
                writeEntryToCache(decoded_brick_start_uint, i, brick_palette.buf[paletteE + palette_delta + 2u]);
            }
#endif

            // The region is constant if the stop bit is set. All following child voxels are set to the current value.
            if ((operation & STOP_BIT) > 0u) {
                uint current_output_voxel_value = readEntryFromCache(decoded_brick_start_uint, i);
                for (uint n = i; n < i + index_step; n++) {
                    writeEntryToCache(decoded_brick_start_uint, n, current_output_voxel_value);
                }
            }
        }
        // The next LoD block uses half the block width and an eigths of the index_step respectively.
        index_step /= 8u;
        lod_width /= 2u;
    }
}

#endif // no RANDOM_ACCESS
// RANDOM ACCESS DECODING ----------------------------------------------------------------------------------------------
#ifdef RANDOM_ACCESS

#ifdef DECODE_FROM_SHARED_MEMORY
    #define BRICK_ENCODING SHARED_BRICK_ENCODING
    #define UNPACK_4BIT_FROM_ENC(entry_id)  _unpack4BitFromEncoding(entry_id)
    #define RANK_4BIT_PALETE_ADV(enc_operation_index) _rank_palette_adv(enc_operation_index)
#else
    #define BRICK_ENCODING brick_encoding.buf
    #define UNPACK_4BIT_FROM_ENC(entry_id)  _unpack4BitFromEncoding(brick_encoding, entry_id)
    #define RANK_4BIT_PALETE_ADV(enc_operation_index) _rank_palette_adv(brick_encoding, enc_operation_index)
#endif

uint _unpack4BitFromEncoding(
                             #ifndef DECODE_FROM_SHARED_MEMORY
                                const EncodingRef brick_encoding,
                             #endif
                             uint entry_id) {
    return bitfieldExtract(BRICK_ENCODING[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}


/** number of PALETTE_ADV occurrences before enc_operation_index. */
uint _rank_palette_adv(
                      #ifndef DECODE_FROM_SHARED_MEMORY
                         const EncodingRef brick_encoding,
                      #endif
                      uint enc_operation_index
                      ) {
    uint occurrences = 0u;
    for(uint entry_id = HEADER_SIZE * 8u; entry_id < enc_operation_index; entry_id++) {
        if ((UNPACK_4BIT_FROM_ENC(entry_id) & 7u) == PALETTE_ADV)
            occurrences++;
    }
    return occurrences;
}


uint getPaletteIndexOfCSGVVoxel(const uint output_i, const uint target_inv_lod,
                            #ifndef DECODE_FROM_SHARED_MEMORY
                                const EncodingRef brick_encoding,
                            #endif
                            const uint brick_encoding_length) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint inv_lod_op_i = output_i;

    // obtain encoding operation read index (4 bit)
    uint enc_operation_index = BRICK_ENCODING[inv_lod] + inv_lod_op_i;
    uint operation = UNPACK_4BIT_FROM_ENC(enc_operation_index);

    assert(enc_operation_index < brick_encoding_length * 8u, "brick encoding out of bounds read");

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        // equal to (operation != PALETTE_LAST && operation != PALETTE_ADV && operation != PALETTE_D)
        while (operation < 4u) {
            // find the read position for the next operation along the chain
            if (operation == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
                inv_lod_op_i /= 8u;
            }
            // operation is NEIGHBOR_X, NEIGHBOR_Y, or NEIGHBOR_Z:
            else {
                // read from a neighbor in the next iteration
                const uint neighbor_index = operation - NEIGHBOR_X;// X: 0, Y: 1, Z: 2
                const uint child_index = inv_lod_op_i % 8u;

                const uvec3 inv_lod_voxel = uvec3(ivec3(_cache_idx2pos(inv_lod_op_i)) + neighbor[child_index][neighbor_index]);
                inv_lod_op_i = _cache_pos2idx(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                }
            }

            // at this point: inv_lod, and inv_lod_op_i must be valid and set correctly!
            enc_operation_index = BRICK_ENCODING[inv_lod] + inv_lod_op_i;
            operation = UNPACK_4BIT_FROM_ENC(enc_operation_index);

            assert(enc_operation_index < brick_encoding_length * 8u, "enc_operation_idx out of bounds");
            assertf(enc_operation_index != 0u || operation == PALETTE_ADV, "first brick operation must be PALETTE_ADV but is %u", operation);
        }
        assert(operation != PALETTE_D, "palette delta operation not supported in parallel decode");
        assert((operation & STOP_BIT) == 0u, "stop bit not supported with Nibble parallel decoding");


        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        const uint palette_index = RANK_4BIT_PALETE_ADV(enc_operation_index);
        // the actual palette index may be offset depending on the operation
        if (operation == PALETTE_LAST) {
            return palette_index - 1u;
        } else {
            return palette_index;
        }
    }
}

#if CACHE_MODE == CACHE_BRICKS

/** Decode a single voxel with index output_i in the target_inv_lod. Decoding is performed by chasing the operation
 * references from the output voxel to a palette reference. If DECODE_FROM_SHARED_MEMORY is set, it is assumed that the
 * brick encoding is located in a shared memory buffer uint SHARED_BRICK_ENCODING[]. */
void decompressCSGVVoxelToCache(const uint output_i, const uint target_inv_lod, const uvec3 valid_brick_size,
#ifndef DECODE_FROM_SHARED_MEMORY
                         const EncodingRef brick_encoding,
#endif
                         const uint brick_encoding_length, const uint decoded_brick_start_uint) {

    uint palette_index = getPaletteIndexOfCSGVVoxel(output_i, target_inv_lod,
                                                #ifndef DECODE_FROM_SHARED_MEMORY
                                                    brick_encoding,
                                                #endif
                                                    brick_encoding_length);

        // Write to the index in the output array. The output array's positions are in Morton order.
#ifdef PALETTE_CACHE
    // TODO: This is a race condition! Different threads write to (different bits of) the same uint in the cache
    writeEntryToCache(decoded_brick_start_uint, output_i, palette_index + 1u);
#else
    writeEntryToCache(decoded_brick_start_uint, output_i, BRICK_ENCODING[brick_encoding_length - 1u - palette_index]);
#endif
}

#endif // CACHE_MODE == CACHE_BRICKS

#ifndef DECODE_FROM_SHARED_MEMORY
uint decompressCSGVVoxel(const uint brick_idx, const uvec3 brick_voxel, const uint target_inv_lod) {
    assertf(brick_idx < g_brick_idx_count, "brick idx out of bounds (is, bound) %v2u", uvec2(brick_idx, g_brick_idx_count));
    assertf(all(lessThan(brick_voxel, uvec3(BRICK_SIZE))), "brick voxel out of brick size bounds, is: %v3u", brick_voxel);

    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);

    const uint lod_width = BRICK_SIZE >> target_inv_lod;
    const uint voxel_idx = _cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width);
    // same as:
    // const uint voxel_idx = _cache_pos2idx(brick_voxel) / (1u << (3 * (findMSB(BRICK_SIZE) - target_inv_lod)));

    const uint palette_index = getPaletteIndexOfCSGVVoxel(voxel_idx, target_inv_lod,
                                                           brick_encoding, brick_encoding_length);

    assertf(palette_index < getBrickPaletteLength(brick_idx),
            "palette index out of palette bounds (brick, palette_idx, palette_length): %v3u",
            uvec3(brick_idx, palette_index, getBrickPaletteLength(brick_idx)));
    return brick_encoding.buf[brick_encoding_length - 1u - palette_index];
}
#endif

#endif // RANDOM_ACCESS

// DEBUGGING AND STATISTICS --------------------------------------------------------------------------------------------

bool verifyBrickCompression(const uint brick_idx) {

    // Obtain a reference to the uint buffer containing this bricks encoding.
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
    const uint brick_encoding_length = getBrickEncodingLength(brick_idx);
    const uint header_start_lods = LOD_COUNT;

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < HEADER_SIZE + 1u + 1u) {
        debugPrintfEXT("brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette) = %u but is %u", HEADER_SIZE + 2u, brick_encoding_length);
        return false;
    }

    // check first header entry being HEADER_SIZE * 8
    if(brick_encoding.buf[0] != HEADER_SIZE * 8u) {
        debugPrintfEXT("first encoding starts 4bit must be header*8");
        return false;
    }

    // check encoding starts being in ascending order
    // note: the header count the number of entries, except the last entry when using double table rANS
    // for which this entry refers to the raw 4 bit index at which the detail encoding starts AFTER packing the earlier LoDs
    for (uint l = 1; l < header_start_lods; l++) {
        uint distance = brick_encoding.buf[l] - brick_encoding.buf[l - 1];
        if(distance < 0u) {
            debugPrintfEXT("encoding starts are not in ascending order");
            return false;
        }
        else if(distance > BRICK_SIZE * BRICK_SIZE * BRICK_SIZE) {
            debugPrintfEXT("encoding starts between LoDs are too far away");
            return false;
        }
    }

    // Brick headers do no longer store LOD palette starts
//    // check palette start of first LoD being 0 and second LoD being 1
//    if (brick_encoding.buf[header_start_lods] != 0u) {
//        debugPrintfEXT("first palette start must be 0");
//        return false;
//    }
//    if (brick_encoding.buf[header_start_lods + 1u] != 1u) {
//        debugPrintfEXT("second palette start must be 1");
//        return false;
//    }

    //    // check palette starts being in ascending order
    //    for(int l = 2u; l <= LOD_COUNT + 1; l++) {
    //        if(brick_encoding[header_start_lods + l] < brick_encoding[header_start_lods + l - 1]) {
    //            error << "  palette starts are not in ascending order\n";
    //            break;
    //        }
    //    }

    uint palette_size = brick_encoding.buf[PALETTE_SIZE_HEADER_INDEX];
    // check palette size not being zero
    if (palette_size == 0u) {
        debugPrintfEXT("palette size is zero");
        return false;
    }

    //    // check palette size + encoding start of last LoD being shorter than the brick encoding
    //    if (palette_size + brick_encoding[header_start_lods]/8u > brick_encoding_length) {
    //        error << "  palette size and encoding of first (L-1) levels are longer than the total brick encoding\n";
    //    }

    return true;
}


#endif // NIBBLE_DECODER_GLSL
