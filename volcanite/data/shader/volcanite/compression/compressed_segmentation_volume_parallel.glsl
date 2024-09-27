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

#ifndef COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL
#define COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL

#ifndef CSGV_DECODING_ARRAY
 #define CSGV_DECODING_ARRAY g_decoding
#endif

#ifndef SHARED_BRICK_ENCODING
 #define SHARED_BRICK_ENCODING s_brick_encoding
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
    return 1u;
//    uint cache_uint_to_read = decoded_brick_start_uint                            // start uint
//                              + cache_idx_in_brick / g_cache_indices_per_uint;    // uint within the cache region
//    // Return a palette index in [1, brickPaletteLength]
//    return bitfieldExtract(CSGV_DECODING_ARRAY[cache_uint_to_read],
//                            int((cache_idx_in_brick % g_cache_indices_per_uint) * g_cache_palette_idx_bits),
//                            int(g_cache_palette_idx_bits));
#else
    return CSGV_DECODING_ARRAY[decoded_brick_start_uint + cache_idx_in_brick];
#endif // ifdef PALETTE_CACHE
}

#ifndef CSGV_READ_ONLY
/** Writes the label entry to the cache cache_idx_in_brick element in the brick region at decoded_brick_start_uint.
 * If a palletized cache is used, writes the least significant g_cache_palette_idx_bits bits from entry instead. */
void writeEntryToCache(uint decoded_brick_start_uint, uint cache_idx_in_brick, uint entry) {
#ifdef PALETTE_CACHE
    assert(false, "PALETTE_CACHE does not support parallel decoding because of race conditions when writing neighboring output voxels to the same uint");
//    uint cache_uint_to_write = decoded_brick_start_uint                            // start uint
//                               + cache_idx_in_brick / g_cache_indices_per_uint;    // uint within the cache region
//    CSGV_DECODING_ARRAY[cache_uint_to_write] =
//                            bitfieldInsert(CSGV_DECODING_ARRAY[cache_uint_to_write],
//                                            entry,
//                                            int((cache_idx_in_brick % g_cache_indices_per_uint) * g_cache_palette_idx_bits),
//                                            int(g_cache_palette_idx_bits));
#else
    CSGV_DECODING_ARRAY[decoded_brick_start_uint + cache_idx_in_brick] = entry;
#endif // ifdef PALETTE_CACHE
}
#endif // ifndef CSGV_READ_ONLY

#ifdef PALETTE_CACHE
/** Returns the label for the voxel position within the brick starting at the given base element.
 * @param decoded_inv_lod the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
 * @param brick_voxel the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod */
uint readCSGVPaletteBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint brick_start_base_element, const uint brick_idx) {
    // Determine which index element to read from the cache region.
    uint lod_width = BRICK_SIZE >> decoded_inv_lod;
    uint cache_idx_in_brick = _cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width);

    // By design, the first palette index is 1, meaning it can be substract directly from the brick's encoding length.
    uint palette_idx = readEntryFromCache(brick_start_base_element * g_cache_base_element_uints, cache_idx_in_brick);
    assertf(palette_idx > 0 && palette_idx <= getBrickPaletteLength(brick_idx), "read palette index %u which is 0 or greater than palette size from cache", palette_idx);
    return getBrickEncodingRef(brick_idx).buf[getBrickEncodingLength(brick_idx) - palette_idx];
}
#else
/** Returns the label for the voxel position within the brick starting at the given base element.
 * @param decoded_inv_lod the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
 * @param brick_voxel the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod */
uint readCSGVBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint brick_start_base_element) {
    // ToDo: why pass the decoded_inv_lod and the decoded_brick_star_base_element? just pass the brick_idx and read it from the cache info here
    uint lod_width = BRICK_SIZE >> decoded_inv_lod;
    return readEntryFromCache(brick_start_base_element,
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

uint _readOperationFromEncoding(uint entry_id) {
    // ToDo: this is where the implementation of access(i) of the wavelet tree goes
    return bitfieldExtract(SHARED_BRICK_ENCODING[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}

/** Fills the brick's cache region by setting all entries to value. */
void fillCSGVBrick(const uint decoded_brick_start_uint, const uint inv_lod, const uint value) {
    uint voxel_count = 1u << (3u * inv_lod);
    for(uint i = 0; i < voxel_count; i++) {
        writeEntryToCache(decoded_brick_start_uint, i, value);
    }
}



#endif // ifndef CSGV_READ_ONLY

#endif // COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL
