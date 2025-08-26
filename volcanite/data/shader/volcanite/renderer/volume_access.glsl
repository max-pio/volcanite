#include "cpp_glsl_include/csgv_constants.incl"

#ifdef EMPTY_SPACE_UINT_SIZE
    #include "volcanite/bit_vector.glsl"
#endif

/// Returns the iLOD into which the brick of the voxel is currently decoded.
uint get_decoded_inv_lod(ivec3 voxel) {
    const uint brick_idx = brick_pos2idx(uvec3(voxel) / BRICK_SIZE, g_brick_count);
    const uint cur_inv_lod = g_brick_info[brick_idx * 4u + BRICK_INFO_CUR_INV_LOD];
    return cur_inv_lod >= LOD_COUNT ? 0u : cur_inv_lod;
}

/// Computes the inverse LOD for the renderer at the given voxel location.
/// The iLOD is constant within each brick and computed from the distance of the brick center to the camera.
/// The corsest iLOD, where one lable represents the whole brick is 0. The finest iLOD is (LOD_COUNT - 1)
uint get_inv_lod(ivec3 voxel) {
    // Ensure the same level-of-detail for all bricks by computing it from the brick's center.
    // Otherwise, different levels-of-detail would be requested for decompression of the same brick.
    const vec3 brick_center_world_space = (g_model_to_world_space * vec4((voxel/BRICK_SIZE)*BRICK_SIZE + BRICK_SIZE/2, 1.f)).xyz;

    // for each axis g_voxels_per_pixel_per_dist stores how many voxels an image pixel footprint overlaps per camera distance
    // we compute the number of voxels per pixel for all axes at once. for isotropic voxel sizes we get .x == .y == .z
    const vec3 dist_xyz = (brick_center_world_space - g_camera_position_world_space);
    const vec3 voxels_per_pixel = length(dist_xyz) * g_voxels_per_pixel_per_dist;

    // ..and then interpolate between these values based on the ray direction
    // bias weights towards the component with the highest contribution (and get absolute values for free)
    vec3 weight = dist_xyz * dist_xyz;

    // use the MSB as a fast approximation for log2 (rounding down to int anyways)
    const int lod_interpolated = findMSB(max(0, int(g_lod_bias + dot(voxels_per_pixel, weight) / (weight.x + weight.y + weight.z))));
    // clamp with a potential user specified max inverse. LoD
    return min(LOD_COUNT - 1u - uint(clamp(lod_interpolated, 0, LOD_COUNT - 1)), g_max_inv_lod);

    // old isotropic computation with single distance:
    // float dist = int(g_voxels_per_pixel_per_dist * length(brick_center_world_space - g_camera_position_world_space))
    // return min(LOD_COUNT - 1u - uint(clamp(g_lod_bias + findMSB(int(dist)), 0, LOD_COUNT - 1u)), g_max_inv_lod);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////                                              NO CACHE                                                      //////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if CACHE_MODE == CACHE_NOTHING
    // returns a label for this voxel but sets valid to invalid if it was read from a brick that was not decoded up to the requested level
    uint get_volume_label(ivec3 voxel, inout float depth_valid) {
        assert(all(greaterThanEqual(voxel.xyz, ivec3(0))) && all(lessThan(voxel.xyz, ivec3(g_vol_dim))), "trying to read volume label for out of bounds voxel!");

        const uvec3 brick = uvec3(voxel) / BRICK_SIZE;
        const uvec3 brick_voxel = uvec3(voxel) - (brick * BRICK_SIZE);
        const uint brick_idx = brick_pos2idx(brick, g_brick_count);
        const uint req_inv_lod = get_inv_lod(voxel);

        return decompressCSGVVoxel(brick_idx, brick_voxel, req_inv_lod);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////                                            VOXEL CACHE                                                     //////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#elif CACHE_MODE == CACHE_VOXELS

//    #undef CACHE_UVEC2_SIZE
//    #define CACHE_UVEC2_SIZE 134217728
// voxel cache sizes > 1 GB significantly decrese the performance
#if CACHE_UVEC2_SIZE > 134217728
    #define TARGET_CACHE_BLOCK_SIZE 134217728
    #define CACHE_BLOCK_COUNT ((CACHE_UVEC2_SIZE + TARGET_CACHE_BLOCK_SIZE - 1) / TARGET_CACHE_BLOCK_SIZE)
    #define CACHE_BLOCK_SIZE (CACHE_UVEC2_SIZE / CACHE_BLOCK_COUNT)
    #if CACHE_BLOCK_COUNT == 1
        #undef CACHE_BLOCK_COUNT
    #endif
#else
    #define CACHE_BLOCK_SIZE CACHE_UVEC2_SIZE
#endif


    uint _hash(uint a, uint x) {
        const uint bits = findMSB(CACHE_BLOCK_SIZE);
        // (ax mod 2^w) / 2^(w-bits)
        return (a * x) >> (31 - bits); // mod 2^32 implicit
    }

    uvec3 _hash(uvec3 a, uvec3 x) {
        const uint bits = findMSB(CACHE_BLOCK_SIZE);
        // (ax mod 2^w) / 2^(w-bits)
        return (a * x) >> (31 - bits); // mod 2^32 implicit
    }

    uvec4 _hash(uvec4 a, uvec4 x) {
        const uint bits = findMSB(CACHE_BLOCK_SIZE);
        // (ax mod 2^w) / 2^(w-bits)
        return (a * x) >> (31 - bits); // mod 2^32 implicit
    }

    // Cuckoo Hashing
    // use three xor-combined (c,k) hash functions to construct the two hash functions:
    // see: "Rasmus Pagh and Flemming Friche Rodler. Cuckoo Hashing. In Journal of Algorithms, pages 122–144 (2004)
    // and: "An Overview of Cuckoo Hashing" by Charles Chen

    // use a component-wise xor as in Hoskins 2014, Quilez 2017a cf. "Hash Functions for GPU Rendering" JCGT 9(3) (2020)
    uint hash1(uvec3 lod_voxel) {
        lod_voxel = _hash(uvec3(19u, 47u, 101u), hash_pcg3d(lod_voxel));
        return (lod_voxel.x ^ lod_voxel.y ^ lod_voxel.z + 131u) % CACHE_BLOCK_SIZE;
        // return (_hash(19u, voxel_id) ^ _hash(3u, voxel_id) ^ _hash(115u, voxel_id)) % CACHE_UVEC2_SIZE;
    }

    uint key(uvec4 lod_voxel_and_lod) {
        lod_voxel_and_lod = _hash(uvec4(5u, 149u, 61u, 2887u), hash_pcg4d(lod_voxel_and_lod));
        return lod_voxel_and_lod.x ^ lod_voxel_and_lod.y ^ lod_voxel_and_lod.z ^ lod_voxel_and_lod.w + 171u;
    }

    // obtains a key and a position in table 1 and maps it to a new position in table 2
    uint hash2(uvec2 key1_pos1) {
        key1_pos1 = hash_pcg2d(key1_pos1);
        uvec4 h = _hash(uvec4(7u, 31u, 97u, 173u), key1_pos1.xyxy);
        return (h.x ^ h.y ^ h.z ^ h.w) % CACHE_BLOCK_SIZE;
        //return (_hash(31u, voxel_id) ^ _hash(7u, voxel_id) ^ _hash(97u, voxel_id)) % CACHE_UVEC2_SIZE;
    }


    // returns a label for this voxel but sets valid to invalid if it was read from a brick that was not decoded up to the requested level
    uint get_volume_label(ivec3 voxel, inout float depth_valid) {
        assert(all(greaterThanEqual(voxel.xyz, ivec3(0))) && all(lessThan(voxel.xyz, ivec3(g_vol_dim))), "trying to read volume label for out of bounds voxel!");

        const uint req_inv_lod = get_inv_lod(voxel);

        // if (req_inv_lod < g_max_inv_lod) {
        //     const uvec3 brick = uvec3(voxel) / BRICK_SIZE;
        //     const uvec3 brick_voxel = uvec3(voxel) - (brick * BRICK_SIZE);
        //     const uint brick_idx = brick_pos2idx(brick, g_brick_count);
        //     return decompressCSGVVoxel(brick_idx, brick_voxel, req_inv_lod);
        // }

        // check if the voxel is flagged as empty space
        // the empty space bit vector index is the morton index of the voxel divided by the set size.
        // the empty space set size is a power of two <= the brick size. all voxels in the same set belong to the same
        // brick and are thus always accessed in the same LOD
#ifdef EMPTY_SPACE_UINT_SIZE
        uvec3 empty_space_base_voxel = uvec3(voxel) / g_empty_space_block_dim;
        const uint empty_space_idx = empty_space_base_voxel.x  // * g_empty_space_dot_map.x is always 1
                                    + empty_space_base_voxel.y * g_empty_space_dot_map.y
                                    + empty_space_base_voxel.z * g_empty_space_dot_map.z;
        assertf(empty_space_idx < EMPTY_SPACE_BV_BIT_SIZE,
                "accessing empty space idx out of bounds (idx, bound): %v2u",
                uvec2(empty_space_idx, EMPTY_SPACE_BV_BIT_SIZE));
        BitVectorRef empty_space_bv = BitVectorRef(g_empty_space_bv_address);
        if (BV_ACCESS(empty_space_bv.words, empty_space_idx) > 0u)
            return INVISIBLE_LABEL;
#endif

        // map voxels from different LODs at the same area to identical indices.
        const uvec3 lod_voxel = (uvec3(voxel) >> (g_max_inv_lod - req_inv_lod)) << (g_max_inv_lod - req_inv_lod);
        const uvec3 brick = uvec3(voxel) / BRICK_SIZE;
        const uint brick_idx = brick_pos2idx(brick, g_brick_count);

        #define KEY_KEY_BITS 0x7FFFFFFFu
        #define KEY_TABLE2_BIT 0x80000000u

        // the 1D cache_key identifies cache collisions between different voxel positions.
        // the MSB of the key is 1 if and only if the element it belongs to is stored at its second position (table 2)
        // the cache position is not directly dependent on the cache_key.
        const uint cache_key = key(uvec4(lod_voxel, req_inv_lod)) & KEY_KEY_BITS;

        // cache is split into multiple smaller hash tables (blocks)
#ifdef CACHE_BLOCK_COUNT
        const uint cache_block_pos = (brick_idx * CACHE_BLOCK_COUNT / g_brick_idx_count) * CACHE_BLOCK_SIZE;
#else
        const uint cache_block_pos = 0u;
#endif
        const uint cache_pos = hash1(lod_voxel) + cache_block_pos;
        assertf(cache_pos < CACHE_UVEC2_SIZE, "invalid cache_pos %u", cache_pos);

        // check if the cache contains the voxel at the first or second lookup pos
        uvec2 cache_elem = unpack32(CSGV_DECODING_ARRAY[cache_pos]);
        // at the first position, the key should have its MSB set to 0
        if (cache_elem.x == cache_key) {
            return cache_elem.y;
        }
        else {
            uvec2 cache_elem2 = unpack32(CSGV_DECODING_ARRAY[hash2(uvec2(cache_key, cache_pos)) + cache_block_pos]);
            // at the second positoin, the key should have its MSB set to 1
            if (cache_elem2.x == (cache_key | KEY_TABLE2_BIT)) {
                return cache_elem2.y;
            }
        }

        // otherwise, decode the element..
        const uvec3 brick_voxel = uvec3(voxel) - (brick * BRICK_SIZE);
        const uint label = decompressCSGVVoxel(brick_idx, brick_voxel, req_inv_lod); // this is extremely expensive!

//    #define CACHE_EJECT_PROB ((~0u) / 8u)
//    #define CACHE_EJECT_PROB 0
    #ifdef CACHE_EJECT_PROB
        {
            const uvec2 rnd = hash_pcg2d(uvec2(cache_key, g_frame));
            // only one work item in the subgroup is allowed to write to the cache
//            if (gl_WorkGroupID.x + gl_WorkGroupID.y * gl_WorkGroupSize.x != (rnd.x % (gl_WorkGroupSize.x * gl_WorkGroupSize.y))) {
//                return label;
//            }
            // if an element would be ejected by inserting the new label, only insert with a certain probability
            if (cache_elem.x != INVALID && rnd.y >= CACHE_EJECT_PROB) {
                return label;
            }
        }
    #endif

        // if the label is not visible, check if all other labels from the same set are not visible as well
#ifdef EMPTY_SPACE_UINT_SIZE
        if (!isLabelVisible(label)) {
            bool all_invisible = true;
            // the base voxel is now the first (lowest coordinate component-wise) local voxel in the brick
            empty_space_base_voxel = (empty_space_base_voxel * g_empty_space_block_dim) % BRICK_SIZE;
            for (uint set_i = 0u; set_i < g_empty_space_set_size; set_i++) {
                const uvec3 s_brick_voxel = empty_space_base_voxel
                                      + uvec3(set_i / (g_empty_space_block_dim * g_empty_space_block_dim),
                                              (set_i / (g_empty_space_block_dim)) % g_empty_space_block_dim,
                                              set_i % g_empty_space_block_dim);

                // empty space set voxels outside of the volume always count as "invisible"
                if (any(greaterThanEqual(brick * BRICK_SIZE + s_brick_voxel, g_vol_dim)))
                    continue;
                // the brick voxel itself was already decoded
                if (all(equal(brick_voxel, s_brick_voxel)))
                    continue;

                // for the empty-space info: decode all voxels in this empty sapce set on the finest LOD.
                // this is extremely expensive!
                if (isLabelVisible(decompressCSGVVoxel(brick_idx, s_brick_voxel, g_max_inv_lod))) {
                    // TODO: it would be possible to add the label to the cache here
                    all_invisible = false;
                    break;
                }
            }
            if (all_invisible) {
                BV_SET1(empty_space_bv.words, empty_space_idx);
                return INVISIBLE_LABEL;
            }
        }
#endif

        // insert the element at its 1st cache position and obtain the (possibly) replaced element
        cache_elem = unpack32(atomicExchange(CSGV_DECODING_ARRAY[cache_pos], pack64(uvec2(cache_key, label))));

        if (cache_elem.x != INVALID && (cache_elem.x & KEY_TABLE2_BIT) == 0u) {
            // only if the ejected element is not already at its second location:
            // insert the ejected element to its 2nd cache position, possibly forcing another element out
            atomicExchange(CSGV_DECODING_ARRAY[hash2(uvec2(cache_elem.x, cache_pos)) + cache_block_pos], pack64(uvec2(cache_elem.x | KEY_TABLE2_BIT, cache_elem.y)));

            // if the element already was at its second location, it will be re-inserted at its first position if it is
            // accessed again. this results in the cuckoo ping-pong loop between first and second position of elements.
        }

        return label;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////                                            BRICK CACHE                                                     //////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#elif CACHE_MODE == CACHE_BRICKS
    /// returns a label for this voxel but sets valid to invalid if it was read from a brick that was not decoded up to the requested level
    /// if REQUEST_ENABLE_BIT is set in request_invalid_bits, the brick is requested for decompression if not available
    /// if INVALIDATE_ENABLE_BIT is set in request_invalid_bits, depth_valid is set to INVALID_DEPTH if birkc is not available
    uint get_volume_label(const ivec3 voxel, inout float depth_valid, const uint request_invalid_bits) {
        assert(all(greaterThanEqual(voxel.xyz, ivec3(0))) && all(lessThan(voxel.xyz, ivec3(g_vol_dim))), "trying to read volume label for out of bounds voxel!");

        const uvec3 brick = uvec3(voxel) / BRICK_SIZE;
        const uvec3 brick_voxel = uvec3(voxel) - (brick * BRICK_SIZE);
        const uint brick_idx = brick_pos2idx(brick, g_brick_count);
        const uint brick_info_pos = brick_idx * 4u;

        // mark brick as visible
        const uint old_req_inv_lod = g_brick_info[brick_info_pos + BRICK_INFO_REQ_INV_LOD];
        assert(old_req_inv_lod <= LOD_COUNT, "trying to access a brick that's flagged as invisible");
        if (old_req_inv_lod == LOD_COUNT    // brick is not yet requested
            #ifdef VALID_RAY_REQUEST_ONLY
                // without checking for validity, may (rarely) request bricks that are actually obscured by bricks
                // in front of them. limiting requests to valid rays, however, creates popping artifacts at
                // interfaces between LODs when the camera moves: bricks behind other bricks are not flagged as
                // visible (because the brick in front is an out-of-date LOD)
                // TODO: this may currently be a source for normal vector artifacts from DDA traversal
                && isDepthValid(depth_valid)
            #else
                // by default: request bricks with invalid rays as well except if brick request limitation is enabled
                && (isDepthValid(depth_valid) || g_req_limit_area_size == 0u)
            #endif
                && (request_invalid_bits & REQUEST_ENABLE_BIT) > 0u) { // this path may still request new brick

            // we set the requested LOD to 0 (does not matter as long as it is < LOD_COUNT) as it will actually be
            // computed in the csgv_request stage of the next frame.
            atomicCompSwap(g_brick_info[brick_info_pos + BRICK_INFO_REQ_INV_LOD], old_req_inv_lod, 1);
        }

        // read voxel from cache (or decoding if the brick is not decoded)
        const uint cur_inv_lod = g_brick_info[brick_info_pos + BRICK_INFO_CUR_INV_LOD];
        if (cur_inv_lod == 0u) {
            // if the corasest LOD is "decoded", its single label is directly storead as "cache index"
            return g_brick_info[brick_info_pos + BRICK_INFO_CACHE_INDEX];
        } else if (cur_inv_lod >= LOD_COUNT) {
            // invalidate the sample if the brick is not decoded yet
            if (bool(request_invalid_bits & INVALIDATE_ENABLE_BIT))
                depth_valid = INVALID_DEPTH;

            // we could alternatively perform a small linear search for the first *visible* label here
            return getBrickEncodingRef(brick_idx).buf[getBrickEncodingLength(brick_idx) - 1u];
        } else {
            #ifdef PALETTE_CACHE
                return readCSGVPaletteBrick(brick_voxel, cur_inv_lod, g_brick_info[brick_info_pos + BRICK_INFO_CACHE_INDEX] * g_cache_base_element_uints, brick_idx);
            #else
                return readCSGVBrick(brick_voxel, cur_inv_lod, g_brick_info[brick_info_pos + BRICK_INFO_CACHE_INDEX] * g_cache_base_element_uints);
            #endif
        }
    }
#endif


bool isEmptySpace(const ivec3 voxel) {
    assertf(all(greaterThanEqual(voxel, ivec3(0))) && all(lessThan(voxel, g_vol_dim)), "empty space request for invalid voxel %v3i", voxel);

    const uvec3 brick = uvec3(voxel / BRICK_SIZE);
    const uint brick_info_pos = brick_pos2idx(brick, g_brick_count) * 4u;

    // if the brick contains nothing visible, it is empty space
    return g_brick_info[brick_info_pos + BRICK_INFO_REQ_INV_LOD] > LOD_COUNT;  // marked "invisible" by request shader
}
