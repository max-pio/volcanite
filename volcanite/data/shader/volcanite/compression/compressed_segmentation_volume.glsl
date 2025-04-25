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

#include "cpp_glsl_include/csgv_constants.incl"

#include "volcanite/compression/csgv_utils.glsl"
#include "morton.glsl"

// some compile time invariants
#if defined(RANDOM_ACCESS) && defined(PALETTE_CACHE)
    #error "random access cannot be used with palette cache"
#endif

#if !defined(RANDOM_ACCESS) && defined(DECODE_FROM_SHARED_MEMORY)
    #error "DECODE_FROM_SHARED_MEMORY can only be used with RANDOM_ACCESS"
#endif


// Read Decoded Bricks (Cache Read) ------------------------------------------------------------------------------------

uint _cache_pos2idx(const uvec3 voxel_pos_in_brick) {
    return morton3Dp2i(voxel_pos_in_brick);
}

#if CACHE_MODE == CACHE_BRICKS

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
uint readCSGVPaletteBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint decoded_brick_start_uint, const uint brick_idx) {
    // TODO: why pass decoded_inv_lod and decoded_brick_start_uint? pass the brick_idx and read cache info here
    // Determine which index element to read from the cache region.
    uint lod_width = BRICK_SIZE >> decoded_inv_lod;
    uint cache_idx_in_brick = _cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width);

    // By design, the first palette index is 1, meaning it can be substract directly from the brick's encoding length.
    uint palette_idx = readEntryFromCache(decoded_brick_start_uint, cache_idx_in_brick);
    assertf(palette_idx > 0 && palette_idx <= getBrickPaletteLength(brick_idx),
            "read palette index is 0 or greater than palette size from cache (idx, palette size) = %v2u",
            uvec2(palette_idx, getBrickPaletteLength(brick_idx)));
    return getBrickEncodingRef(brick_idx).buf[getBrickEncodingLength(brick_idx) - palette_idx];
}
#else
/** Returns the label for the voxel position within the brick starting at the given base element.
 * @param decoded_inv_lod the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
 * @param brick_voxel the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod */
uint readCSGVBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint decoded_brick_start_uint) {
    // TODO: why pass decoded_inv_lod and decoded_brick_start_uint? pass the brick_idx and read cache info here
    uint lod_width = BRICK_SIZE >> decoded_inv_lod;
    return readEntryFromCache(decoded_brick_start_uint,
                              (_cache_pos2idx(brick_voxel) / (lod_width * lod_width * lod_width)));
}
#endif // ifdef PALETTE_CACHE

#endif // CACHE_MODE == CACHE_BRICKS
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


#if CACHE_MODE == CACHE_BRICKS
uint _valueOfNeighborCache(const uvec3 brick_pos, const uint local_lod_i, const uint lod_width, const int neighbor_i, const uint decoded_brick_start_uint) {
    // Find the position of the neighbor and convert it to a memory index.
    ivec3 neighbor_pos = ivec3(brick_pos) + neighbor[local_lod_i][neighbor_i] * int(lod_width);
    assertf(all(greaterThanEqual(neighbor_pos, ivec3(0))) && all(lessThan(neighbor_pos, ivec3(BRICK_SIZE))), "neighbor voxel %v3i out of brick bounds", neighbor_pos);
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

#endif // CACHE_MODE == CACHE_BRICKS

#if ENCODING_MODE == NIBBLE_ENC
    #include "volcanite/compression/decoder/NibbleDecoder.glsl"
#elif ENCODING_MODE == SINGLE_TABLE_RANS_ENC || ENCODING_MODE == DOUBLE_TABLE_RANS_ENC
    #include "volcanite/compression/decoder/RangeANSDecoder.glsl"
#elif ENCODING_MODE == WAVELET_MATRIX_ENC
    #include "volcanite/compression/decoder/WaveletMatrixDecoder.glsl"
#elif ENCODING_MODE == HUFFMAN_WM_ENC
    #include "volcanite/compression/decoder/HuffmanWMDecoder.glsl"
#else
    #error "unknown or no CSGV decoder specified in shader"
#endif

#endif // ifndef CSGV_READ_ONLY

#endif // COMPRESSED_SEGMENTATION_VOLUME_GLSL
