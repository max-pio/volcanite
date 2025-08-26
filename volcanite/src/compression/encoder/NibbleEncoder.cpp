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

#include "volcanite/compression/encoder/NibbleEncoder.hpp"
#include "volcanite/compression/VolumeCompressionBase.hpp"
#include "volcanite/compression/memory_mapping.hpp"
#include "volcanite/compression/pack_nibble.hpp"
#include <omp.h>

namespace volcanite {

uint32_t NibbleEncoder::readNextLodOperationFromEncoding(const uint32_t *brick_encoding, ReadState &state) const {
    return read4Bit(brick_encoding, 0u, state.idxE++);
}

// BRICK MEMORY LAYOUT for L = log2(brick_size) LODs
// HEADER                 ENCODING:
// 4bit_encoding_start[0, 1, .. L-1], palette_start[0, 1 .. L], 4bit_encoding_padded_to32bit[0, 1, .. L], 32bit_palette[L, .., 1, 0]
//       header_size*8 ᒧ                always zero ᒧ  ∟ .. one  ∟ palette size
uint32_t NibbleEncoder::encodeBrickForRandomAccess(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out,
                                                   const glm::uvec3 start, const glm::uvec3 volume_dim) const {
    assert(!(m_op_mask & OP_STOP_BIT) && "Nibble encoder does not support stop bits with random access");
    assert(!(m_op_mask & OP_PALETTE_D_BIT) && "Nibble encoder does not support palette delta operation with random access");

    std::vector<uint32_t> palette;
    palette.reserve(32);

    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = getHeaderSize();
    uint32_t out_i = header_size * 8u; // write head position in out, counted as number of encoded 4 bit elements

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value = INVALID;
    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    VolumeCompressionBase::constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size, m_op_mask & OP_STOP_BIT, true);

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    out[0] = out_i;      // LoD start position
    out[lod_count] = 0u; // palette start position (from back)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    if (multigrid[muligrid_lod_start].constant_subregion) {
        write4Bit(out, 0u, out_i++, PALETTE_ADV | STOP_BIT);
    } else {
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
        // out[lod_count + current_inv_lod] = static_cast<uint32_t>(palette.size()); (no longer writing LOD palette sizes)

        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        const uint32_t lod_dim = (m_brick_size / lod_width);
        const uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for a grid node that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write PARENT_STOP.
            glm::uvec3 brick_pos = enumBrickPos(i);

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            const uint32_t child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
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

            uint32_t value = multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].label;
            assert(value != INVALID && "Original volume mustn't contain the INVALID magic value!");

            uint32_t operation = 0u;
            // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick area of the subtree
            // note that grid nodes outside the volume are by definition also homogeneous
            if (lod_width > 1 && multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) {
                operation = STOP_BIT;
            }
            // determine operation for the next entry
            [[likely]]
            if ((m_op_mask & OP_PARENT_BIT) && value == parent_value)
                operation |= PARENT;
            else if ((m_op_mask & OP_NEIGHBORX_BIT) && valueOfNeighbor(&multigrid[muligrid_lod_start], &multigrid[parent_multigrid_lod_start], brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if ((m_op_mask & OP_NEIGHBORY_BIT) && valueOfNeighbor(&multigrid[muligrid_lod_start], &multigrid[parent_multigrid_lod_start], brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if ((m_op_mask & OP_NEIGHBORZ_BIT) && valueOfNeighbor(&multigrid[muligrid_lod_start], &multigrid[parent_multigrid_lod_start], brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if ((m_op_mask & OP_PALETTE_LAST_BIT) && palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                // Random access encoding does not use the palette delta operation
                { // if nothing helps, we add a completely new palette entry
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }
            assert(operation < 16u && "writing invalid 4 bit operation!");
            write4Bit(out, 0u, out_i++, operation);

            assert(value != INVALID);
        }
        current_inv_lod++;
    }

    // last entry of our header stores the palette size
    out[CSGVSerialBrickEncoder::getPaletteSizeHeaderIndex()] = palette.size();
    // now we calculate everything in 32 bit elements. round up to start the palette at an uint32_t index but AFTER the last encoding element
    while (out_i % 8u != 0u)
        write4Bit(out, 0u, out_i++, 0u);
    out_i /= 8u;
    // palette is added in reverse order at the end to be read from encoding back to front
    for (int i = static_cast<int>(palette.size()) - 1; i >= 0; i--) {
        out.at(out_i++) = palette.at(i);
    }

    if (out_i >= out.size())
        throw std::runtime_error("out doesn't provide enough memory for encoded brick, writing outside of allocated region");
    return out_i;
}

/// Dummy method to replace the rank operation for querying palette indices when a plain 4 bit encoding is used.
/// @return the number of PALETTE_ADV occurrences before enc_operation_index.
uint32_t rank_palette_adv_4bit(const uint32_t *brick_encoding, uint32_t enc_operation_index) {
    uint32_t occurrences = 0u;
    const uint32_t header_size = brick_encoding[0];
    for (uint32_t entry_id = header_size; entry_id < enc_operation_index; entry_id++) {
        if (read4Bit(brick_encoding, 0u, entry_id) == PALETTE_ADV)
            occurrences++;
    }
    return occurrences;
}

uint32_t NibbleEncoder::decompressCSGVBrickVoxel(const uint32_t output_i, const uint32_t target_inv_lod,
                                                 const glm::uvec3 valid_brick_size, const uint32_t *brick_encoding,
                                                 const uint32_t brick_encoding_length) const {
    // Start by reading the operations in the target inverse LoD's encoding:
    uint32_t inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint32_t inv_lod_op_i = output_i;

    // obtain encoding operation read index (4 bit)
    uint32_t enc_operation_index = brick_encoding[inv_lod] + inv_lod_op_i;
    uint32_t operation = read4Bit(brick_encoding, 0u, enc_operation_index);

    assert(enc_operation_index < brick_encoding_length * 8u && "brick encoding out of bounds read");

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
                const uint32_t neighbor_index = operation - NEIGHBOR_X; // X: 0, Y: 1, Z: 2
                const uint32_t child_index = inv_lod_op_i % 8u;

                const glm::uvec3 inv_lod_voxel = glm::uvec3(glm::ivec3(enumBrickPos(inv_lod_op_i)) + neighbor[child_index][neighbor_index]);
                inv_lod_op_i = indexOfBrickPos(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], glm::ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                }
            }

            // at this point: inv_lod, inv_lod_op_i, and inv_lod_voxel must be valid and set correctly!
            enc_operation_index = brick_encoding[inv_lod] + inv_lod_op_i;
            operation = read4Bit(brick_encoding, 0u, enc_operation_index);
        }
        assert(operation != PALETTE_D && "palette delta operation not supported with random access");
        assert((read4Bit(brick_encoding, 0u, enc_operation_index) & STOP_BIT) == 0u && "stop bit not supported with random access in Nibble encoder");

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint32_t palette_index = rank_palette_adv_4bit(brick_encoding, enc_operation_index);
        // the actual palette index may be offset depending on the operation
        if (operation == PALETTE_LAST) {
            palette_index--;
        }

        // Write to the index in the output array. The output array's positions are in Morton order.
        return brick_encoding[brick_encoding_length - 1u - palette_index];
    }
}

void NibbleEncoder::parallelDecodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                        uint32_t *output_brick, glm::uvec3 valid_brick_size, const int target_inv_lod) const {
    // ToDo: support detail separation, stop bits, and palette delta operations in parallelDecodeBrick
    assert(!m_separate_detail && "detail separation not yet supported in parallelDecodeBrick");
    assert(target_inv_lod < getLodCountPerBrick() && "not enough LoDs in a brick to process target inv. LoD");

    // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    // #pragma omp parallel for default(none) shared(m_brick_size, output_brick)
    // for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++)
    //    output_brick[i] = INVALID;

    const uint32_t output_voxel_count = 1u << (3u * target_inv_lod);
    const uint32_t target_brick_size = 1u << target_inv_lod;

    // output array is filled in an á-trous manner. A target_brick_size < m_brick_size will leave gaps in the output brick.
    const uint32_t output_index_step = (m_brick_size / target_brick_size) * (m_brick_size / target_brick_size) *
                                       (m_brick_size / target_brick_size);

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

} // namespace volcanite
