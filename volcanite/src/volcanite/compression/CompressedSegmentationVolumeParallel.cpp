#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include <map>
#include <sstream>
#include <unordered_set>
#include <thread>

#include "volcanite/compression/pack_nibble.hpp"

namespace volcanite {

uint32_t encodingIndexOfNeighbor(const uint32_t index, const int neighbor_i) {
    throw std::runtime_error("not implemented");
}

// BRICK MEMORY LAYOUT for L = log2(brick_size) LODs
// HEADER                 ENCODING:
// 4bit_encoding_start[0, 1, .. L-1], palette_start[0, 1 .. L], 4bit_encoding_padded_to32bit[0, 1, .. L], 32bit_palette[L, .., 1, 0]
//       header_size*8 ᒧ                always zero ᒧ  ∟ .. one  ∟ palette size
uint32_t CompressedSegmentationVolume::encodeBrickForRandomAccess(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out, glm::uvec3 start, glm::uvec3 volume_dim) {
    std::vector<uint32_t> palette;
    palette.reserve(32);
    glm::uvec3 volume_pos, brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = getHeaderSize();
    uint32_t out_i = header_size * 8u;  // write head position in out, counted as number of encoded 4 bit elements

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index; // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size);

    // ToDo: random access encoding does not support stop bits yet
    for (MultiGridNode &node: multigrid)
        node.constant_subregion = false;

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    out[0] = out_i;                 // LoD start position
    out[lod_count] = 0u;            // palette start position (from back)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    if (multigrid[muligrid_lod_start].constant_subregion) {             //isHomogeneousBrick(volume, volume_dim, glm::uvec3(0u), {brick_size, brick_size, brick_size})) {Z
        write4Bit(out, 0u, out_i++, PALETTE_ADV | STOP_BIT);
    }
    else {
        write4Bit(out, 0u, out_i++, PALETTE_ADV);
    }
    palette.push_back(multigrid[muligrid_lod_start].label);


    // DEBUG
    uint32_t parent_counter = 0;

    // now we iteratively refine from coarse (8 elements in the brick) to finest (brick_size^3 elements in the brick) levels
    uint32_t current_inv_lod = 1u;
    for (uint32_t lod_width = m_brick_size / 2u; lod_width > 0u; lod_width /= 2u) {
        // write to header: keep track of where the new LODs start as number of 4bit
        out[current_inv_lod] = out_i;
        out[lod_count + current_inv_lod] = static_cast<uint32_t>(palette.size());

        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size/lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        bool in_detail_lod = (m_rANS_mode == DOUBLE_TABLE_RANS) && (current_inv_lod == lod_count - 1u);

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for a grid node that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write PARENT_STOP.
            brick_pos = enumBrickPos(i);
            volume_pos = start + brick_pos;
            if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
            if (child_index == 0) {
                assert(parent_counter <= 8 && "parent element would be used for more than 8 elements!");

                // if this subtree is already filled (because in a previous LOD we set a PARENT_STOP for this area), the last element of this block is set, and we can skip it
                // note that this will also happen if this grid node lies completely outside the volume because some parent would've been set to PARENT_STOP earlier
                // our parent spanned 8 elements of this finer current level, so we need to look at the element 7 indices further
                if (multigrid[parent_multigrid_lod_start + voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].constant_subregion) {
                    parent_counter = 0;
                    i += (lod_width * lod_width * lod_width * 7);
                    continue;
                }

                parent_counter = 0;
                parent_value = multigrid[parent_multigrid_lod_start + voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))].label;
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }
            parent_counter++;

            value = multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].label;
            assert(value != INVALID && "Original volume mustn't contain the INVALID magic value!");

            uint32_t operation = 0u;
            // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick area of the subtree
            // note that grid nodes outside the volume are by definition also homogeneous
            if (lod_width > 1 && multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) {
                operation = STOP_BIT;
            }
            // determine operation for the next entry
            [[likely]]
            if (value == parent_value)
                operation |= PARENT;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if (valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if (palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                // Random access encoding does not use the palette delta operation
                // reuse the n-X palette value where 0 < X < 17
//                uint32_t palette_delta = static_cast<uint32_t>(std::find(palette.rbegin(), palette.rend(), value) - palette.rbegin());
//                if(m_use_palette_delta && palette_delta < 17u && palette_delta < palette.size()) {
//                    assert(palette.at(palette.size() - palette_delta - 1u) == value && "Palette value does not fit!");
//                    assert(palette_delta > 0u && "the palette delta 0 should've been caught by the palette_last value!");
//                    write4Bit(out, 0u, out_i++, operation | PALETTE_D);
//                    operation = palette_delta - 1u; // the "0" case is already handled by PALETTE_LAST, so we only consider case 1 - 16 in our 4 bits
//                } else
                {  // if nothing helps, we add a completely new palette entry
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }
            assert(operation < 16u && "writing invalid 4 bit operation!");
            write4Bit(out, 0u, out_i++, operation);

            assert(value != INVALID);
        }


//        if(m_rANS_mode == DOUBLE_TABLE_HUFFMAN_WT) {
//            throw std::runtime_error("DOUBLE_TABLE_HUFFMAN_WT not implemented yet");
//            // pack all previous levels via rANS encoding if we're at the second last LoD (last LoD of non-detail encoding)
//            // NOTE: the old out_i and header starts count in number of elements. the following out_i counts in 4bit
//            if (current_inv_lod == lod_count - 2u) {
//                out_i = m_rans.packRANS(out, out[0], out_i);
//                // the detail encoding has to start at a new 32bit element (which is guaranteed by our rANS output)
//                assert(out_i % 8u == 0u && "next element after rANS output should start at a new uint32_t element");
//            }
//            // pack the detail (=finest LOD) via rANS encoding.
//            // We have a separate rANS encoder here because the detail level does not use stop bits => different operation frequencies
//            else if (in_detail_lod) {
//                out_i = m_detail_rans.packRANS(out, out[current_inv_lod], out_i);
//            }
//        }
        current_inv_lod++;
    }

    // if we did not apply the rANS packing before, because we are only using a single freq. table, we do it here
//    if(m_rANS_mode == WT)
//        out_i = m_rans.packRANS(out, out[0], out_i);



    // last entry of our header stores the palette size
    out[header_size - 1u] = palette.size();
    // now we calculate everything in 32 bit elements. round up to start the palette at an uint32_t index but AFTER the last encoding element
    while(out_i % 8u != 0u)
        write4Bit(out, 0u, out_i++, 0u);
    out_i /= 8u;
    // palette is added in reverse order at the end to be read from encoding back to front
    for(int i = static_cast<int>(palette.size()) - 1; i >= 0; i--) {
        out.at(out_i++) = palette.at(i);
    }

    if(out_i >= out.size())
        throw std::runtime_error("out doesn't provide enough memory for encoded brick, writing outside of allocated region");
    return out_i; // we return the number of uint32_t elements that we used
}

/// @return the number of PALETTE_ADV occurrences before enc_operation_index. */
uint32_t rank_palette_adv(const uint32_t* brick_encoding, uint32_t enc_operation_index) {
    // TODO: good lord this is expensive if we do it without an O(1) rank
    uint32_t occurrences = 0u;
    const uint32_t header_size = brick_encoding[0];
    for(uint32_t entry_id = header_size; entry_id <= enc_operation_index; entry_id++) {
        if ((read4Bit(brick_encoding, 0u, entry_id) & 7u) == PALETTE_ADV)
            occurrences++;
    }
    return occurrences;
}

uint32_t CompressedSegmentationVolume::decompressCSGVBrickVoxel(const uint32_t output_i, const uint32_t target_inv_lod,
                                                                const glm::uvec3 valid_brick_size,
                                                                const uint32_t* brick_encoding,
                                                                const uint32_t brick_encoding_length) const {
    assert(m_random_access &&
            "Random access voxel decompression within a brick is only available with random access enabled.");

    // Start by reading the operations in the target inverse LoD's encoding:
    uint32_t inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint32_t inv_lod_op_i = output_i;
    // corresponding voxel position within the inv. LoD
    glm::uvec3 inv_lod_voxel = enumBrickPos(inv_lod_op_i);

    // obtain encoding operation read index (4 bit)
    uint32_t enc_operation_index = brick_encoding[inv_lod] + inv_lod_op_i;
    uint32_t operation = read4Bit(brick_encoding, 0u, enc_operation_index);

    assert(enc_operation_index < brick_encoding_length * 8u && "brick encoding out of bounds read");
    // ToDo: handle stop bits
    assert((operation & STOP_BIT) == 0u && "stop bit not yet supported with random access");

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits

        // equal to (operation_lsb != PALETTE_LAST && operation_lsb != PALETTE_ADV && operation_lsb != PALETTE_D)
        while (operation_lsb < 4u) {
            // find the read position for the next operation along the chain
            if (operation_lsb == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
                inv_lod_op_i /= 8u;
                inv_lod_voxel = enumBrickPos(inv_lod_op_i);
            }
                // operation_lsb is NEIGHBOR_X, NEIGHBOR_Y, or NEIGHBOR_Z:
            else {
                // read from a neighbor in the next iteration
                const uint32_t neighbor_index = operation_lsb - NEIGHBOR_X; // X: 0, Y: 1, Z: 2
                const uint32_t child_index = inv_lod_op_i % 8u;

                inv_lod_voxel += neighbor[child_index][neighbor_index];
                inv_lod_op_i = indexOfBrickPos(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], glm::ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                    inv_lod_voxel = enumBrickPos(inv_lod_op_i);
                }
            }

            // at this point: inv_lod, inv_lod_op_i, and inv_lod_voxel must be valid and set correctly!
            enc_operation_index = brick_encoding[inv_lod] + inv_lod_op_i;
            operation_lsb = read4Bit(brick_encoding, 0u, enc_operation_index) & 7u;
        }

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint32_t palette_index = rank_palette_adv(brick_encoding, enc_operation_index - 1u);
        // the actual palette index may be offset depending on the operation
        if (operation_lsb == PALETTE_LAST) {
            palette_index--;
        }
        assert(operation_lsb != PALETTE_D && "palette delta operation not supported with random access");
        //assert(palette_index < getBrickPaletteLength(brick_idx), "obtained wrong palette index");

        // Write to the index in the output array. The output array's positions are in Morton order.
        return brick_encoding[brick_encoding_length - 1u - palette_index];
    }
}

void CompressedSegmentationVolume::parallelDecodeBrick(uint32_t brick_idx, uint32_t* output_brick, glm::uvec3 valid_brick_size, int target_inv_lod) const {
    assert(m_random_access && "parallel brick decompression is only supported when parallel_decode is set");
    assert(m_rANS_mode == NO_RANS && "parallel decode does not work using rANS");
    // ToDo: support detail separation, stop bits, and palette delta operations in parallelDecodeBrick
    assert(!m_separate_detail && "detail separation not yet supported in parallelDecodeBrick");
    assert(target_inv_lod < getLodCountPerBrick() && "not enough LoDs in a brick to process target inv. LoD");
    assert(glm::all(glm::equal(valid_brick_size, glm::uvec3(m_brick_size))) && "partially occupied bricks are not allowed");

    const uint32_t brick_encoding_length = m_brick_starts[brick_idx + 1u] - m_brick_starts[brick_idx];
    const uint32_t *brick_encoding = getBrickEncoding(brick_idx);

    // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    #pragma omp parallel for default(none) shared(m_brick_size, output_brick)
    for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++)
        output_brick[i] = INVALID;

    const uint32_t output_voxel_count = 1u << (3u * target_inv_lod);
    const uint32_t target_brick_size = 1u << target_inv_lod;

    // output array is filled in an á-trous manner. A target_brick_size < m_brick_size will leave gaps in the output brick.
    const uint32_t output_index_step = (m_brick_size / target_brick_size) * (m_brick_size / target_brick_size) * (m_brick_size / target_brick_size);

    // m_cpu_threads many threads go through the Morton indexing order from front to back. The threads work on the next
    // following items in parallel. read_offset is the index of the first thread 0.
    //
    // Of course, we could directly parallelize over the number of output voxels in a for loop here, but:
    // on a GPU m_cpu_threads should be equal to the number of threads in a warp allowing us to do vulkan subgroup optimizations
    #pragma omp parallel num_threads(m_cpu_threads) default(none) shared(output_index_step, output_voxel_count, target_inv_lod, brick_encoding, output_brick, target_brick_size, brick_encoding_length)
    {
        uint32_t output_i = omp_get_thread_num();
        while (output_i < output_voxel_count) {
            output_brick[output_index_step * output_i] =
                    decompressCSGVBrickVoxel(output_i, target_inv_lod, glm::uvec3(m_brick_size),
                                             brick_encoding, brick_encoding_length);

            // #pragma omp barrier
            output_i += omp_get_num_threads();
        }
    }
}



void CompressedSegmentationVolume::parallelDecompressLOD(int target_lod, std::vector<uint32_t>& out) const {
    if (!m_random_access)
        throw std::runtime_error("Parallel decompression requires previous compression with random access enabled.");

    const glm::uvec3 brickCount = getBrickCount();
    uint32_t inv_lod = getLodCountPerBrick() - 1u - target_lod;
    assert(inv_lod >= 0);

    glm::uvec3 brick_pos;
#ifndef NO_BRICK_DECODE_INDEX_REMAP
    std::vector<uint32_t> brick_cache(m_brick_size * m_brick_size * m_brick_size);  // brick output in morton order
#endif

    // we iterate over all bricks and decompress brick voxels in parallel
    for (brick_pos.z = 0; brick_pos.z < brickCount.z; brick_pos.z++) {
        for (brick_pos.y = 0; brick_pos.y < brickCount.y; brick_pos.y++) {
            for (brick_pos.x = 0; brick_pos.x < brickCount.x; brick_pos.x++) {
                size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
#ifndef NO_BRICK_DECODE_INDEX_REMAP
                // decode brick with threads parallelizing over the output voxels
                parallelDecodeBrick(brick_idx, brick_cache.data(), glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inv_lod);
                // fill output array with decoded brick entries
                for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
                    glm::uvec3 out_pos = brick_pos * m_brick_size + enumBrickPos(i);
                    if (glm::all(glm::lessThan(out_pos, m_volume_dim))) {
                        out[voxel_pos2idx(out_pos, m_volume_dim)] = brick_cache[i];
                    }
                }
#else
                parallelDecodeBrick(brick_idx, &(out[pos2idx(brick_pos * m_brick_size, m_volume_dim)]), glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inv_lod);
#endif
            }
        }
    }
}



void CompressedSegmentationVolume::freqEncodeBrickForRandomAccess(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const {
    throw std::runtime_error("rANS compression is not supported for in-brick parallel decoding.");
}


} // namespace vvv
