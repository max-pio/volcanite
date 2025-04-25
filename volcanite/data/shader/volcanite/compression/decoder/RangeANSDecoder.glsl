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

#ifndef RANGE_ANS_DECODER_GLSL
#define RANGE_ANS_DECODER_GLSL

#include "volcanite/compression/csgv_utils.glsl"
#include "volcanite/compression/rans.glsl"

/// This Decoder corresponds to the encoding from ../../../../include/volcanite/compression/encoder/RangeANSEncoder.hpp
/// Supported configuration compile time defines:
///   PALETTE_CACHE
///   SEPARATE_DETAIL

#if ENCODING_MODE != SINGLE_TABLE_RANS_ENC && ENCODING_MODE != DOUBLE_TABLE_RANS_ENC
    #error "expected SINGLE_TABLE_RANS_ENC or DOUBLE_TABLE_RANS_ENC encoding mode"
#endif

#ifdef RANDOM_ACCESS
    #error "RangeANS encoding does not support RANDOM_ACCESS"
#endif

#ifdef DECODE_FROM_SHARED_MEMORY
    #error "RangeANS encoding does not support DECODE_FROM_SHARED_MEMORY"
#endif

#if CACHE_MODE != CACHE_BRICKS
    #error "RangeANS encoding only supports CACHE_MODE set to CACHE_BRICKS"
#endif

#if defined(SEPARATE_DETAIL) && ENCODING_MODE != DOUBLE_TABLE_RANS_ENC
    #error "SEPARATE_DETAIL only supported in double table RangeANS encoding"
#endif


// UTILITY FUNCTIONS ---------------------------------------------------------------------------------------------------

uint _unpack4BitFromEncoding(EncodingRef brick_start, uint entry_id) {
    return bitfieldExtract(brick_start.buf[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}

// SERIAL ENCODING -----------------------------------------------------------------------------------------------------

struct CSGVReadState {
    uint idxE;              ///< read position in brick encoding (counted in nibbles without rANS, in bytes with rANS)
    uint rans_state;        ///< rANS decoder state
    uint rans_tab_offset;   ///< rANS frequency table lookup offset: 0u in base levels, 17u in second detail level table
};

uint _readNextLodOperationFromEncoding(EncodingRef brick_start, inout CSGVReadState state) {
    return rans_itr_nextSymbol(state.rans_state, brick_start, state.idxE, state.rans_tab_offset);
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
    CSGVReadState readState;    // read and changed in the _readNextLodOperationFromEncoding function

    readState.idxE = brick_encoding.buf[start_at_inv_lod];  // current read entry, in number of nibbles (4 bit)
    readState.idxE = (readState.idxE / 8u) * 4u;            // current read entry, in number of bytes (8 bit)
    readState.rans_tab_offset = 0u;
    rans_itr_initDecoding(readState.rans_state, brick_encoding, readState.idxE);

    uint output_size = (1u << target_inv_lod); // voxel count in each output brick dim. for LoD. BRICK_SIZE on finest LoD.
    uint lod_width = (1u << target_inv_lod) / (1u << start_at_inv_lod);
    uint index_step = (lod_width * lod_width * lod_width);
    uint parent_value;

    // Brick encoding order goes from the coarsest inverse LoD (0) to the finest invese LoD (LOD_COUNT - 1).
    for(uint inv_lod = start_at_inv_lod; inv_lod <= target_inv_lod; inv_lod++) {
#if ENCODING_MODE == DOUBLE_TABLE_RANS_ENC
        if(inv_lod == LOD_COUNT - 1u) {
            // Use the detail freq. table (which is offset by 17) from now on.
            readState.rans_tab_offset = 17u;
            #ifdef SEPARATE_DETAIL
                brick_encoding = getBrickDetailEncodingRef(brick_idx);
                readState.idxE = 0u;
            #else
                // Detail level rANS encoding starts at a new uint
                readState.idxE = (brick_encoding.buf[inv_lod] / 8u) * 4u;
            #endif
            rans_itr_initDecoding(readState.rans_state, brick_encoding, readState.idxE);
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

// RANDOM ACCESS DECODING ----------------------------------------------------------------------------------------------

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
    }

    // Brick headers do no longer store palette offsets
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

#endif // RANGE_ANS_DECODER_GLSL
