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

#ifndef COMPRESSED_SEGMENTATION_VOLUME_GLSL
#define COMPRESSED_SEGMENTATION_VOLUME_GLSL

#ifndef CSGV_DECODING_ARRAY
 #define CSGV_DECODING_ARRAY g_decoding
#endif

#include "cpp_glsl_include/csgv_constants.h"

#include "csgv_utils.glsl"
#include "morton.glsl"
#ifdef USE_RANS
    #include "rans.glsl"
#endif

// Read Decoded Bricks (Cache Read) ------------------------------------------------------------------------------------

uint _cache_pos2idx(const uvec3 voxel_pos_in_brick) {
    return morton3Dp2i(voxel_pos_in_brick);
}

/** Returns the label at the cache_idx_in_brick-th entry from the brick's cache region. The region starts at
 * decoded_brick_start_uint in the cache array. If a palletized cache is used, the palette index of the label is
 * returned instead. */
uint readEntryFromCache(uint decoded_brick_start_uint, uint cache_idx_in_brick) {
#ifdef PALETTE_CACHE
    uint cache_uint_to_read = decoded_brick_start_uint                            // start uint
                              + cache_idx_in_brick / g_cache_indices_per_uint;    // uint within the cache region
    // Return a palette index in [1, brickPaletteLength]
    return bitfieldExtract(CSGV_DECODING_ARRAY[cache_uint_to_read],
                            int((cache_idx_in_brick % g_cache_indices_per_uint) * g_cache_palette_idx_bits),
                            int(g_cache_palette_idx_bits));
#else
    return CSGV_DECODING_ARRAY[decoded_brick_start_uint + cache_idx_in_brick];
#endif // ifdef PALETTE_CACHE
}

#ifndef CSGV_READ_ONLY
/** Writes the label entry to the cache cache_idx_in_brick element in the brick region at decoded_brick_start_uint.
 * If a palletized cache is used, writes the least significant g_cache_palette_idx_bits bits from entry instead. */
void writeEntryToCache(uint decoded_brick_start_uint, uint cache_idx_in_brick, uint entry) {
#ifdef PALETTE_CACHE
    uint cache_uint_to_write = decoded_brick_start_uint                            // start uint
                               + cache_idx_in_brick / g_cache_indices_per_uint;    // uint within the cache region
    CSGV_DECODING_ARRAY[cache_uint_to_write] =
                            bitfieldInsert(CSGV_DECODING_ARRAY[cache_uint_to_write],
                                            entry,
                                            int((cache_idx_in_brick % g_cache_indices_per_uint) * g_cache_palette_idx_bits),
                                            int(g_cache_palette_idx_bits));
#else
    CSGV_DECODING_ARRAY[decoded_brick_start_uint + cache_idx_in_brick] = entry;
#endif // ifdef PALETTE_CACHE
}
#endif // ifdef CSGV_READ_ONLY

#ifdef PALETTE_CACHE
/** Returns the label for the voxel position within the brick starting at the given base element.
 * @param decoded_inv_lod the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
 * @param brick_voxel the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod */
uint readCSGVPaletteBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint brick_start_base_element, const uint brick_idx) {
    // Determine which index element to read from the cache region.
    uint lod_width = g_brick_size >> decoded_inv_lod;
    uint cache_idx_in_brick = _cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width);

    // By design, the first palette index is 1, meaning it can be substract directly from the brick's encoding length.
    uint palette_idx = readEntryFromCache(brick_start_base_element * g_cache_base_element_uints, cache_idx_in_brick);
    assertf(palette_idx > 0 && palette_idx <= getBrickPaletteLength(brick_idx), "read palette index %u is 0 or greater than palette size from cache", palette_idx);
    return getBrickEncodingRef(brick_idx).buf[getBrickEncodingLength(brick_idx) - palette_idx];
}
#else
/** Returns the label for the voxel position within the brick starting at the given base element.
 * @param decoded_inv_lod the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
 * @param brick_voxel the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod */
uint readCSGVBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint brick_start_base_element) {
    // TODO: why pass decoded_inv_lod and decoded_brick_start_base_element? pass the brick_idx and read cache info here
    uint lod_width = g_brick_size >> decoded_inv_lod;
    return readEntryFromCache(brick_start_base_element * g_cache_base_element_uints,
                              (_cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width)));
}
#endif // ifdef PALETTE_CACHE

// Decoding (Cache Write) ----------------------------------------------------------------------------------------------
#ifndef CSGV_READ_ONLY

uvec3 _cache_idx2pos(uint i) {
    return morton3Di2p(i);
}

const ivec3 neighbor[8][3] = {  {ivec3(-1, 0, 0), ivec3(0, -1, 0), ivec3(0, 0, -1)},
                                {ivec3( 1, 0, 0), ivec3(0, -1, 0), ivec3(0, 0, -1)},
                                {ivec3(-1, 0, 0), ivec3(0,  1, 0), ivec3(0, 0, -1)},
                                {ivec3( 1, 0, 0), ivec3(0,  1, 0), ivec3(0, 0, -1)},
                                {ivec3(-1, 0, 0), ivec3(0, -1, 0), ivec3(0, 0,  1)},
                                {ivec3( 1, 0, 0), ivec3(0, -1, 0), ivec3(0, 0,  1)},
                                {ivec3(-1, 0, 0), ivec3(0,  1, 0), ivec3(0, 0,  1)},
                                {ivec3( 1, 0, 0), ivec3(0,  1, 0), ivec3(0, 0,  1)}};

uint _unpack4BitFromEncoding(EncodingRef brick_start, uint entry_id) {
    return bitfieldExtract(brick_start.buf[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}

struct CSGVReadState {
    uint idxE;              ///< read position in brick encoding (counted in nibbles without rANS, in bytes with rANS)
    uint rans_state;        ///< rANS decoder state
    uint rans_tab_offset;   ///< rANS frequency table lookup offset: 0u in base levels, 17u in second detail level table
};

uint _readNextLodOperationFromEncoding(EncodingRef brick_start, inout CSGVReadState state) {
#ifdef USE_RANS
    return rans_itr_nextSymbol(state.rans_state, brick_start, state.idxE, state.rans_tab_offset);
#else
    return _unpack4BitFromEncoding(brick_start, state.idxE++);
#endif
}

uint _valueOfNeighbor(const uvec3 brick_pos, const uint local_lod_i, const uint lod_width, const int neighbor_i, const uint decoded_brick_start_uint) {
    // Find the position of the neighbor and convert it to a memory index.
    ivec3 neighbor_pos = ivec3(brick_pos) + neighbor[local_lod_i][neighbor_i] * int(lod_width);
    assertf(all(greaterThanEqual(neighbor_pos, ivec3(0))) && all(lessThan(neighbor_pos, ivec3(g_brick_size))), "neighbor voxel %v3i out of brick bounds", neighbor_pos);
    uint neighbor_index = _cache_pos2idx(uvec3(neighbor_pos));

    // In case the neighbor is not yet decoded on this level (this is the case if neighbor_index > pos_index <=> any
    // element of neighbor[local_lod_i][neighbor_i] is postive), the neighbor's parent's label has to be accessed
    // instead. The parent is at the lower multiple of (lod_width*8) position in the temporary decoding output.
    if(any(greaterThan(neighbor[local_lod_i][neighbor_i], ivec3(0))))
        neighbor_index -= neighbor_index % (lod_width * lod_width * lod_width * 8);

    // Return index of neighbor or parent neighbor within the output brick.
    return readEntryFromCache(decoded_brick_start_uint, neighbor_index);
}

/** Fills the brick's cache region by setting all entries to value. */
void fillCSGVBrick(const uint decoded_brick_start_uint, const uint inv_lod, const uint value) {
    uint voxel_count = 1u << (3u * inv_lod);
    for(uint i = 0; i < voxel_count; i++) {
        writeEntryToCache(decoded_brick_start_uint, i, value);
    }
}

/** Reset the brick's cache region to be used as output for a decompression. */
void resetCSGVBrick(const uint decoded_brick_start_uint, const uint inv_lod) {
#ifdef PALETTE_CACHE
    fillCSGVBrick(decoded_brick_start_uint, inv_lod, 0);
    // ToDo: do a for loop over full uints and set multiple elements to 0 at once
#else
    fillCSGVBrick(decoded_brick_start_uint, inv_lod, INVALID);
#endif
}


/** Decompresses the brick from the encoding array to the cache region at decoded_brick_start_uint up to the given
 * inverse LoD.
 * If start_at_inv_lod == 0, it is assumed that the output brick cache is set to INVALID at all entries.
 * If start_at_inv_lod > 0, it is assumed that the output brick cache is fully decoded up to (start_at_inv_lod-1).
 * Start_at_inv_Lod must not be the finest possible LoD. */
void decompressCSGVBrick(const uint brick_idx, const uint brick_encoding_length,
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

    // the palette starts at the end of the encoding block
#ifdef PALETTE_CACHE
    uint paletteE = 1u;         // 0 is the magic number for unwritten output elements. 1 the first palette entry
#else
    uint paletteE = brick_encoding_length - 1u;
#endif
    CSGVReadState readState;    // read and changed in the _readNextLodOperationFromEncoding function

    // Obtain a reference to the uint buffer containing this bricks encoding.
    EncodingRef brick_encoding = getBrickEncodingRef(brick_idx);
#ifndef PALETTE_CACHE
    EncodingRef brick_palette = brick_encoding;
#endif

    readState.idxE = brick_encoding.buf[start_at_inv_lod];  // current read entry, in number of nibbles (4 bit)
#ifdef USE_RANS
    readState.idxE = (readState.idxE / 8u) * 4u;            // current read entry, in number of bytes (8 bit)
    readState.rans_tab_offset = 0u;
    rans_itr_initDecoding(readState.rans_state, brick_encoding, readState.idxE);
#endif

    uint output_size = (1u << target_inv_lod); // voxel count in each output brick dim. for LoD. g_brick_size on finest LoD.
    uint lod_width = (1u << target_inv_lod) / (1u << start_at_inv_lod);
    uint index_step = (lod_width * lod_width * lod_width);
    uint parent_value;

    // Brick encoding order goes from the coarsest inverse LoD (0) to the finest invese LoD (g_lod_count - 1).
    for(uint inv_lod = start_at_inv_lod; inv_lod <= target_inv_lod; inv_lod++) {
#ifdef USE_RANS_DOUBLE_TABLE
        if(inv_lod == g_lod_count - 1u) {
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
            if (any(greaterThanEqual(_cache_idx2pos(i).xyz * (g_brick_size/output_size), valid_brick_size)))
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
                                  _valueOfNeighbor(_cache_idx2pos(i), local_lod_i, lod_width,
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
                uint palette_delta = _readNextLodOperationFromEncoding(brick_encoding, readState) + 2u;
                writeEntryToCache(decoded_brick_start_uint, i, paletteE - palette_delta);
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
                uint palette_delta = _readNextLodOperationFromEncoding(brick_encoding, readState) + 2u;
                writeEntryToCache(decoded_brick_start_uint, i, brick_palette.buf[paletteE + palette_delta]);
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
#endif // ifndef CSGV_READ_ONLY

#endif // COMPRESSED_SEGMENTATION_VOLUME_GLSL
