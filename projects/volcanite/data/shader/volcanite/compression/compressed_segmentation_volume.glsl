#ifndef COMPRESSED_SEGMENTATION_VOLUME_GLSL
#define COMPRESSED_SEGMENTATION_VOLUME_GLSL

// compile time parameters:
#ifndef CSGV_DECODING_ARRAY
 #define CSGV_DECODING_ARRAY g_decoding
#endif

// 4 bit operations:
#include "cpp_glsl_include/csgv_constants.h"

#include "csgv_utils.glsl"
#include "morton.glsl"
#ifdef USE_RANS
    #include "rans.glsl"
#endif

uint _indexOfBrickPos(const uvec3 p) {
    return morton3Dp2i(p);
}

// returns the label for the voxel position within the brick starting at the start_idx.
// decoded_inv_lod: the state of the brick in CSGV_DECODING_ARRAY *must* be a full decoding up to this inv_lod
// brick_voxel: the coordinate of the lookup voxel on the *finest* lod, even if the lookup is for a coarser lod
uint readCSGVBrick(const uvec3 brick_voxel, const uint decoded_inv_lod, const uint decoded_brick_start_idx) {
    uint lod_width = g_brick_size / (1u << decoded_inv_lod);
    // determine position in brick
    return CSGV_DECODING_ARRAY[decoded_brick_start_idx + (_indexOfBrickPos(brick_voxel) / (lod_width * lod_width * lod_width))];
}


#define CACHE_TIME_MOD 256u

// Unpacks the two properties "birthTime" and "brickID" from the header. Used for debug purposes in rednerer.
void unpackCacheHeader32bit(uint header, out uint birthTime8bit, out uint brickID24bit) {
    brickID24bit = header & 0xFFFFFFu;
    birthTime8bit = (header >> 24);
}
uint unpackBrickIDFromCache(uint header) {
    return header & 0xFFFFFFu;
}
uint unpackBirthTimeFromCache(uint header) {
    return header >> 24;
}

#ifndef CSGV_READ_ONLY

// Packs the two properties in one uint with 8bit birthTime followed by 24bit brickID (LSB)
uint cacheHeader32bit(uint birthTime8bit, uint brickID24bit) {
    return (birthTime8bit & 0xFFu) << 24 | (brickID24bit & 0xFFFFFFu);
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
    uint idxE;
    uint rans_state;
    uint rans_tab_offset;   // 0u if using the normal table, 17u if using the detail LOD table
};

uint _readNextLodOperationFromEncoding(EncodingRef brick_start, inout CSGVReadState state) {
    #ifdef USE_RANS
        // detail separation will be handled in the advance step of the rANS decoder
        return rans_itr_nextSymbol(state.rans_state, brick_start, state.idxE, state.rans_tab_offset);
    #else
        // detail separation is only used in combination with RANS so we don't have to consider it here
        return _unpack4BitFromEncoding(brick_start, state.idxE++);
    #endif
}

uvec3 _enumBrickPos(uint i) {
    return morton3Di2p(i);
}

uint _valueOfNeighbor(const uvec3 brick_pos, const uint local_lod_i, const uint lod_width, const int neighbor_i, const uint decoded_brick_start_idx) {
    // find the position of the neighbor
    ivec3 neighbor_pos = ivec3(brick_pos) + neighbor[local_lod_i][neighbor_i] * int(lod_width);
    // should not happen here! we just decode, assume correct labels
    //    if (any(lessThan(neighbor_pos, ivec3(0))) || any(greaterThanEqual(neighbor_pos, ivec3(brick_size))))
    //        return INVALID;
    // find the index of the neighbor within the brick array
    uint neighbor_index = _indexOfBrickPos(uvec3(neighbor_pos));

    // in case we want to access a neighbor that is not already existing on this level (neighbor_i > our_i or any element of neighbor[local_lod_i][neighbor_i] is postive, we have to
    // round down to the parent element of this element (lod_width*8)
    if(any(greaterThan(neighbor[local_lod_i][neighbor_i], ivec3(0))))
        neighbor_index -= neighbor_index % (lod_width * lod_width * lod_width * 8);

    // return value of neighbor or parent neighbor in brick
    return CSGV_DECODING_ARRAY[decoded_brick_start_idx + neighbor_index];
}

// reset the brick by setting all of its entries to INVALID
void resetCSGVBrick(const uint decoded_brick_start_idx, const uint inv_lod) {
    uint voxel_count = 1u << (3u * inv_lod);
    for(uint i = decoded_brick_start_idx; i < decoded_brick_start_idx + voxel_count; i++) {
        CSGV_DECODING_ARRAY[i] = INVALID;
    }
}

// fills the brick by setting all of its entries to value
void fillCSGVBrick(const uint decoded_brick_start_idx, const uint inv_lod, const uint value) {
    uint voxel_count = 1u << (3u * inv_lod);
    for(uint i = decoded_brick_start_idx; i < decoded_brick_start_idx + voxel_count; i++) {
        CSGV_DECODING_ARRAY[i] = value;
    }
}


// decompresses the encoding of the brick from the encoding array starting at encoding_start_index and ending at
// encoding_end_index to the shard memory brick up to thegiven inverse LOD level.
// the output brick decoding decoded_brick_start_index is used
// if start_at_inv_lod == 0, it is assumed that the output brick cache is set to INVALID at all entries
// if start_at_inv_lod > 0, it is assumed that the output brick cache is fully decoded up to (start_at_inv_lod-1)
// start_at_inv_Lod must not be the finest possible LoD
void decompressCSGVBrick(const uint encoding_start_index, const uint encoding_end_index,
#ifdef SEPARATE_DETAIL
                                  const uint detail_start_index, const uint detail_end_index,
#endif
                                  const uvec3 valid_brick_size, const uint start_at_inv_lod, const uint inv_lod,
                                  const uint decoded_brick_start_idx) {

//    // safe mode test: do not decompress anything, instead fill the voxels with dummy values in [0, 256)
//    fillCSGVBrick(decoded_brick_start_idx, inv_lod, (encoding_start_index / 7) % 256);
//    return;

    // refine up to the LOD that was requested, starting with decoding of start_at_inv_lod
    // the starting position of the current LOD in the encoding array, measured in elements of entry_t. Taken from first brick header entries
    uint local_lod_i;   // the local index of this element within the lod block of the coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index
    // the palette starts at the end of the encoding block
    uint paletteE = encoding_end_index - encoding_start_index - 1u;
    CSGVReadState readState;    // read and changed in the _readNextLodOperationFromEncoding function

    // reference to the uint buffer containing this bricks encoding
    // ToDo: this would be the place to select different buffers if the complete encoding is > 4 GB, e.g. based on the brick_id. Or rather pass it as an argument to the whole function.
    EncodingRef brick_encoding = getBrickEncodingRef(encoding_start_index);
    EncodingRef brick_palette = brick_encoding;

    readState.idxE = brick_encoding.buf[start_at_inv_lod];  // offset of current 4 bit entry to read
    readState.rans_tab_offset = 0u;
#ifdef USE_RANS
    readState.idxE = (readState.idxE / 8u) * 4u;
    rans_itr_initDecoding(readState.rans_state, brick_encoding, readState.idxE);
#endif

    uint output_size = (1u << inv_lod); // the resolution of voxel in each dimension of the output brick = for this LoD. On the finest LoD this is g_brick_size
    uint lod_width = (1u << inv_lod) / (1u << start_at_inv_lod);
    uint index_step = (lod_width * lod_width * lod_width);
    uint parent_value;

    // WE ASSUME that the brick was already completely set to INVALID
    //    // first, set the whole brick to UNASSIGNED so we know later, what elements and LOD blocks were already processed
    //    for(uint i = 0; i < g_brick_size * g_brick_size * g_brick_size; i++)
    //        CSGV_DECODING_ARRAY[i] = UNASSIGNED;

    for(uint lod = start_at_inv_lod; lod <= inv_lod; lod++) {

#ifdef USE_RANS_DOUBLE_TABLE
        // RANS_DOUBLE_TABLE is also always set whenever we use detail separation.
        if(lod == g_lod_count - 1u) {
            readState.rans_tab_offset = 17u;        // we now read from the detail freq. table (which is offset by 17)
            #ifdef SEPARATE_DETAIL
                brick_encoding = getBrickDetailEncodingRef(detail_start_index);
                readState.idxE = 0u;
            #else
                // Detail rANS encoding starts at new uint
                readState.idxE = (brick_encoding.buf[lod] / 8u) * 4u;
            #endif
            rans_itr_initDecoding(readState.rans_state, brick_encoding, readState.idxE);
        }
#endif

        for (uint i = 0u; i < output_size * output_size * output_size; i += index_step) {
            // if an LOD block is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
            if (any(greaterThanEqual(_enumBrickPos(i).xyz * (g_brick_size/output_size), valid_brick_size)))
                continue;

            uint out_i = decoded_brick_start_idx + i;

//            // just fill the brick with random values
//            CSGV_DECODING_ARRAY[out_i] = ((encoding_start_index) % 8u) * 383u + ((i/index_step)%2)*512  + 1u; //brick_(i % 4095u) + 1u;
//            continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            local_lod_i = (i % (index_step*8))/index_step;
            if (lod > 0u && i % (index_step*8) == 0) {
                // if this subtree is already filled (because in a previous LOD we had a PARENT_STOP for this area), the last element of this block is set and we can skip it
                if (CSGV_DECODING_ARRAY[out_i + (index_step*7)] != INVALID) {
                    i += (index_step*7);
                    continue;
                }

                parent_value = CSGV_DECODING_ARRAY[out_i];
            }

            // get the next operation and apply it
            uint operation = _readNextLodOperationFromEncoding(brick_encoding, readState);

            uint operation_lsb = operation & 7u; // extract least significant 3 bits with 0111
            if (operation_lsb == PARENT)
                CSGV_DECODING_ARRAY[out_i] = parent_value;
            else if (operation_lsb == NEIGHBOR_X)
                CSGV_DECODING_ARRAY[out_i] = _valueOfNeighbor(_enumBrickPos(i), local_lod_i, lod_width, 0, decoded_brick_start_idx);
            else if (operation_lsb == NEIGHBOR_Y)
                CSGV_DECODING_ARRAY[out_i] = _valueOfNeighbor(_enumBrickPos(i), local_lod_i, lod_width, 1, decoded_brick_start_idx);
            else if (operation_lsb == NEIGHBOR_Z)
                CSGV_DECODING_ARRAY[out_i] = _valueOfNeighbor(_enumBrickPos(i), local_lod_i, lod_width, 2, decoded_brick_start_idx);
            else if (operation_lsb == PALETTE_ADV) {   // read palette entry and advance palette pointer to the next entry
                CSGV_DECODING_ARRAY[out_i] = brick_palette.buf[paletteE--];
            }
            else if (operation_lsb == PALETTE_LAST) { // reuse the last palette entry
                CSGV_DECODING_ARRAY[out_i] = brick_palette.buf[paletteE+1];
            }
            else if (operation_lsb == PALETTE_D) {
                uint palette_delta = _readNextLodOperationFromEncoding(brick_encoding, readState) + 2u;
                CSGV_DECODING_ARRAY[out_i] = brick_palette.buf[paletteE + palette_delta];
            }

            // stop traversal: fill all other parts of the brick with this value
            if ((operation & STOP_BIT) > 0u) {
                // fill the whole subtree with the parent value
                for (uint n = out_i; n < out_i + index_step; n++) {
                    CSGV_DECODING_ARRAY[n] = CSGV_DECODING_ARRAY[out_i];
                }
            }
        }
        // move to the next LOD block with half the block width and an eight of the index_step respectively
        index_step /= 8u;
        lod_width /= 2u;
    }
}
#endif // CSGV_READ_ONLY

#endif /* COMPRESSED_SEGMENTATION_VOLUME_GLSL */
