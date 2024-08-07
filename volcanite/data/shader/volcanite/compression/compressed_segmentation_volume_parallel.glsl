#ifndef COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL
#define COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL

#ifndef CSGV_DECODING_ARRAY
 #define CSGV_DECODING_ARRAY g_decoding
#endif

#ifndef CSGV_SHARED_MEMORY_BRICK_ENCODING
 #define CSGV_SHARED_MEMORY_BRICK_ENCODING s_brick_encoding
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
    uint lod_width = g_brick_size >> decoded_inv_lod;
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

uint _readOperationFromEncoding(uint entry_id) {
    // ToDo: this is where the implementation of access(i) of the wavelet tree goes
    return bitfieldExtract(CSGV_SHARED_MEMORY_BRICK_ENCODING[entry_id/8], 28 - int(entry_id % 8u) * 4, 4);
}

/** Fills the brick's cache region by setting all entries to value. */
void fillCSGVBrick(const uint decoded_brick_start_uint, const uint inv_lod, const uint value) {
    uint voxel_count = 1u << (3u * inv_lod);
    for(uint i = 0; i < voxel_count; i++) {
        writeEntryToCache(decoded_brick_start_uint, i, value);
    }
}

/** number of PALETTE_ADV occurrences before enc_operation_index. */
uint rank_palette_adv(uint enc_operation_index) {
    // TODO: good lord this is expensive if we do it without an O(1) rank
    uint occurrences = 0u;
    const uint header_size = CSGV_SHARED_MEMORY_BRICK_ENCODING[0];
    for(uint entry_id = header_size; entry_id <= enc_operation_index; entry_id++) {
        if ((_readOperationFromEncoding(entry_id) & 7u) == PALETTE_ADV)
        occurrences++;
    }
    return occurrences;
}

/** Decode a single voxel with index output_i in the target_inv_lod. Decoding is performed by chasing the operation
 * references from the output voxel to a palette reference. It is assumed that the brick encoding is located in a shared
 * memory buffer uint CSGV_SHARED_MEMORY_BRICK_ENCODING[]. */
void decompressCSGVVoxelSharedMemory(const uint output_i, const uint brick_encoding_length,
                         const uvec3 valid_brick_size, const uint target_inv_lod,
                         const uint decoded_brick_start_uint) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint inv_lod_op_i = output_i;
    // corresponding voxel position within the inv. LoD
    uvec3 inv_lod_voxel = _cache_idx2pos(inv_lod_op_i);

    // obtain encoding operation read index (4 bit)
    uint enc_operation_index = CSGV_SHARED_MEMORY_BRICK_ENCODING[inv_lod] + inv_lod_op_i;
    uint operation = _readOperationFromEncoding(enc_operation_index);

    assert(enc_operation_index < brick_encoding_length * 8u, "brick encoding out of bounds read");
    // ToDo: handle stop bits
    assert((operation & STOP_BIT) == 0u, "stop bit not yet supported in parallel decode");

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        uint operation_lsb = operation & 7u; // extract least significant 3 bits

        // equal to (operation_lsb != PALETTE_LAST && operation_lsb != PALETTE_ADV && operation_lsb != PALETTE_D)
        while (operation_lsb < 4u) {
            // find the read position for the next operation along the chain
            if (operation_lsb == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
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

            // at this point: inv_lod, inv_lod_op_i, and inv_lod_voxel must be valid and set correctly!
            enc_operation_index = CSGV_SHARED_MEMORY_BRICK_ENCODING[inv_lod] + inv_lod_op_i;
            operation_lsb = _readOperationFromEncoding(enc_operation_index) & 7u;
        }

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint palette_index = rank_palette_adv(enc_operation_index - 1u);
        // the actual palette index may be offset depending on the operation
        if (operation_lsb == PALETTE_LAST) {
            palette_index--;
        }
        assert(operation_lsb != PALETTE_D, "palette delta operation not supported in parallel decode");
        //assert(palette_index < getBrickPaletteLength(brick_idx), "obtained wrong palette index");

        // Write to the index in the output array. The output array's positions are in Morton order.
#ifdef PALETTE_CACHE
        // TODO: This is a race condition! Different threads write to (different bits of) the same uint in the cache
        writeEntryToCache(decoded_brick_start_uint, output_i, palette_index + 1u);
#else
        writeEntryToCache(decoded_brick_start_uint, output_i, CSGV_SHARED_MEMORY_BRICK_ENCODING[brick_encoding_length - 1u - palette_index]);
#endif
    }
}


#endif // ifndef CSGV_READ_ONLY

#endif // COMPRESSED_SEGMENTATION_VOLUME_PARALLEL_GLSL
