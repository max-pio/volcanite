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

#include "volcanite/compression/encoder/WaveletMatrixEncoder.hpp"
#include "volcanite/compression/VolumeCompressionBase.hpp"
#include "volcanite/compression/pack_nibble.hpp"
#include "volcanite/compression/pack_wavelet_matrix.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"
#include <omp.h>

namespace volcanite {

uint32_t WaveletMatrixEncoder::encodeBrickForRandomAccess(const std::vector<uint32_t> &volume,
                                                          std::vector<uint32_t> &out,
                                                          glm::uvec3 start, glm::uvec3 volume_dim) const {
    assert(!(m_op_mask & OP_PALETTE_D_BIT) && "Wavelet matrix encoder does not support palette delta operation");
    assert(!(m_encoding_mode == WAVELET_MATRIX_ENC && (m_op_mask & OP_STOP_BIT)) && "Wavelet matrix encoder (without Huffman encoding) does not support stop bits");

    std::vector<uint32_t> palette;
    palette.reserve(32);
    glm::uvec3 brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = lod_count + 1u;
    uint32_t out_i = header_size * 8u; // write head position in out, counted as number of encoded 4 bit elements

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index; // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    VolumeCompressionBase::constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size, m_op_mask & OP_STOP_BIT, true);

    // stop bit vector
    BitVector stop_bit_vector;
    if (m_op_mask & OP_STOP_BIT)
        stop_bit_vector.reserve(multigrid.size());

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    out[0] = out_i;      // LoD start position
    out[lod_count] = 0u; // palette start position (from back)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    write4Bit(out, 0u, out_i++, PALETTE_ADV);
    if (multigrid[muligrid_lod_start].constant_subregion) {
        stop_bit_vector.push_back(1);
    } else {
        stop_bit_vector.push_back(0);
    }
    palette.push_back(multigrid[muligrid_lod_start].label);

    // DEBUG
    uint32_t parent_counter = 0;

    // now we iteratively refine from coarse (8 elements in the brick) to finest (brick_size^3 elements in the brick) levels
    uint32_t current_inv_lod = 1u;
    for (uint32_t lod_width = m_brick_size / 2u; lod_width > 0u; lod_width /= 2u) {
        // write to header: keep track of where the new LODs start as number of 4bit
        out[current_inv_lod] = out_i;
        // out[lod_count + current_inv_lod] = static_cast<uint32_t>(palette.size()); (not storing lod palette sizes anymore)

        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size / lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for a grid node that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write PARENT_STOP.
            brick_pos = enumBrickPos(i);

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

            uint32_t operation;
            if (m_op_mask & OP_STOP_BIT) {
                // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick
                // area of the subtree. note that grid nodes outside the volume are by definition also homogeneous
                if (lod_width > 1 && multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) {
                    // wavelet matrix does not store the stop bits per operation code, but in a separate bit vector
                    // (serial encoder behavior would be: operation = STOP_BIT;)
                    stop_bit_vector.push_back(1);
                } else {
                    stop_bit_vector.push_back(0);
                }
            }

            // determine operation for the next entry
            [[likely]]
            if ((m_op_mask & OP_PARENT_BIT) && value == parent_value)
                operation = PARENT;
            else if ((m_op_mask & OP_NEIGHBORX_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation = NEIGHBOR_X;
            else if ((m_op_mask & OP_NEIGHBORY_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation = NEIGHBOR_Y;
            else if ((m_op_mask & OP_NEIGHBORZ_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation = NEIGHBOR_Z;
            else if ((m_op_mask & OP_PALETTE_LAST_BIT) && palette.back() == value)
                operation = PALETTE_LAST;
            else {
                // Random access encoding does not use the palette delta operation
                { // if nothing helps, we add a completely new palette entry
                    palette.push_back(value);
                    operation = PALETTE_ADV;
                }
            }
            assert(operation <= 5u && "writing invalid 4 bit operation!");
            write4Bit(out, 0u, out_i++, operation);

            assert(value != INVALID);
        }

        current_inv_lod++;
    }

    assert(!(m_op_mask & OP_STOP_BIT) || (out_i - out[0]) == stop_bit_vector.size() && "stop bit vector size and operation stream size must be equal");

    // wavelet matrix packing
    if (m_encoding_mode == WAVELET_MATRIX_ENC)
        out_i = packWaveletMatrix(out.data(), out[0], out_i, lod_count);
    else if (m_encoding_mode == HUFFMAN_WM_ENC)
        out_i = packWaveletMatrixHuffman(out.data(), out[0], out_i, lod_count);
    else
        assert(false && "unsupported encoding mode for wavelet matrix encoder");

    // now we calculate everything in 32 bit elements. round up to start the palette at an uint32_t index but AFTER the last encoding element
    while (out_i % 8u != 0u)
        write4Bit(out, 0u, out_i++, 0u);
    out_i /= 8u;

    // append stop bit vector (if stop bits are enabled)
    if (m_op_mask & OP_STOP_BIT) {
        // fill the bit vector so that it contains full words
        // TODO: this is only done as the brick encoding stores the bit vector length as full bit vector words
        //  (translated to a 32 bit index offset). The flat rank size that is recomputed from here must be correct.
        while (stop_bit_vector.size() % (8 * sizeof(BV_WordType)) != 0u)
            stop_bit_vector.push_back(0u);

        // construct and write flat rank information
        FlatRank fr(stop_bit_vector);
        const uint32_t fr_32b_size = fr.getRawDataSize() * (sizeof(BV_L12Type) / sizeof(uint32_t));
        const uint32_t *fr_32b = reinterpret_cast<const uint32_t *>(fr.getRawData());
        for (uint32_t i = 0; i < fr_32b_size; i++) {
            out[out_i++] = fr_32b[i];
        }

        // write bit vector
        assert(sizeof(BV_WordType) % sizeof(uint32_t) == 0u);
        const uint32_t stop_bit_32b_size = stop_bit_vector.getRawDataSize() * (sizeof(BV_WordType) / sizeof(uint32_t));
        const uint32_t *stop_bit_32b = reinterpret_cast<const uint32_t *>(stop_bit_vector.getRawData());
        for (uint32_t i = 0; i < stop_bit_32b_size; i++) {
            out[out_i++] = stop_bit_32b[i];
        }

        // write stop bit vector length. query it at:
        //     brick_end - palette_size
        // start address of the bit vector can be queried as:
        //     brick_end - palette_size - 1 - stop_bit_length
        // flat rank start address is:
        //     stop_bit_address - getFlatRankEntries(stop_bit_32b_size * 32) * sizeof(BV_L12Type) / sizeof(uint32_t)
        out[out_i++] = stop_bit_32b_size;
    }

    // append palette which is added in reverse order at the end to be read from encoding back to front
    // last entry of our header stores the palette size
    out[getPaletteSizeHeaderIndex()] = palette.size();
    for (int i = static_cast<int>(palette.size()) - 1; i >= 0; i--) {
        out.at(out_i++) = palette.at(i);
    }

    if (out_i >= out.size())
        throw std::runtime_error("out doesn't provide enough memory for encoded brick, writing outside of allocated region");
    return out_i; // we return the number of uint32_t elements that we used
}

uint32_t WaveletMatrixEncoder::decompressCSGVBrickVoxelWM(const uint32_t output_i, const uint32_t target_inv_lod,
                                                          const glm::uvec3 valid_brick_size,
                                                          const uint32_t *brick_encoding,
                                                          const uint32_t brick_encoding_length,
                                                          const WMBrickHeader *wm_header,
                                                          const BV_WordType *bit_vector) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint32_t inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint32_t inv_lod_op_i = output_i;
    // corresponding voxel position within the inv. LoD
    glm::uvec3 inv_lod_voxel = enumBrickPos(inv_lod_op_i);

    // obtain encoding operation read index (4 bit)
    assert(brick_encoding[0] == 0u && "First operation in the op.stream must have start index 0.");
    uint32_t enc_operation_index = brick_encoding[inv_lod] + inv_lod_op_i;
    assert(enc_operation_index < brick_encoding_length * 8u && "brick encoding out of bounds read");
    uint32_t operation = wm_access(enc_operation_index, wm_header, bit_vector);

    // TODO: add stop bit handling for plain Wavelet Matrix encoding

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        assert(operation <= PALETTE_LAST && "Wavelet Matrix encoding does not support stop bits encoded in OP stream");

        // equal to (operation_lsb != PALETTE_LAST && operation_lsb != PALETTE_ADV && operation_lsb != PALETTE_D)
        while (operation < 4u) {
            // find the read position for the next operation along the chain
            if (operation == PARENT) {
                // read from the parent in the next iteration
                inv_lod--;
                assert(inv_lod <= target_inv_lod && "LOD chasing overflow for Huffman Wavelet Matrix decoding.");
                inv_lod_op_i /= 8u;
                inv_lod_voxel = enumBrickPos(inv_lod_op_i);
            }
            // operation is NEIGHBOR_X, NEIGHBOR_Y, or NEIGHBOR_Z:
            else {
                // read from a neighbor in the next iteration
                const uint32_t neighbor_index = operation - NEIGHBOR_X; // X: 0, Y: 1, Z: 2
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
            operation = wm_access(enc_operation_index, wm_header, bit_vector) & 7u;
        }

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint32_t palette_index = wm_rank(enc_operation_index, PALETTE_ADV, wm_header, bit_vector);
        // the actual palette index may be offset depending on the operation
        if (operation == PALETTE_LAST) {
            palette_index--;
        }
        // assert(palette_index < brick_encoding_length[getPaletteSizeHeaderIndex()] && "out of bounds palette index");

        // Write to the index in the output array. The output array's positions are in Morton order.
        return brick_encoding[brick_encoding_length - 1u - palette_index];
    }
}

uint32_t WaveletMatrixEncoder::decompressCSGVBrickVoxelWMHuffman(const uint32_t output_i, const uint32_t target_inv_lod,
                                                                 const glm::uvec3 valid_brick_size,
                                                                 const uint32_t *brick_encoding,
                                                                 const uint32_t brick_encoding_length,
                                                                 const WMHBrickHeader *wm_header,
                                                                 const BV_WordType *bit_vector,
                                                                 const FlatRank_BitVector_ptrs &stop_bits) {

    // Start by reading the operations in the target inverse LoD's encoding:
    uint32_t inv_lod = target_inv_lod;
    // operation index within in the current inv. LoD, starting at the target LoD
    uint32_t inv_lod_op_i = output_i;

    // obtain encoding operation read index (4 bit)
    assert(brick_encoding[0] == 0u && "First operation in the op.stream must have start index 0.");

    // if stop bits are enabled, an offset must be subtracted from encoding array indices.
    uint32_t enc_operation_index = stop_bits.bv
                                       ? getEncodingIndexWithStopBits(inv_lod, inv_lod_op_i, brick_encoding, stop_bits)
                                       : brick_encoding[inv_lod] + inv_lod_op_i;

    assert(enc_operation_index < wm_header->level_starts_1_to_4[0] && "brick encoding out of bounds read");
    uint32_t operation = wm_huffman_access(enc_operation_index, wm_header, bit_vector);

    // follow the chain of operations from the current output voxel up to an operation that accesses the palette
    {
        assert(operation <= PALETTE_LAST && "Wavelet Matrix encoding does not support stop bits encoded in OP stream");

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

                // get the current corresponding voxel position within the inv. LoD to add the neighbor offset
                const glm::uvec3 inv_lod_voxel = glm::ivec3(enumBrickPos(inv_lod_op_i)) + neighbor[child_index][neighbor_index];
                inv_lod_op_i = indexOfBrickPos(inv_lod_voxel);

                // ToDo: may be able to remove this later! for neighbors with later indices, we have to copy from its parent instead
                if (any(greaterThan(neighbor[child_index][neighbor_index], glm::ivec3(0)))) {
                    inv_lod--;
                    inv_lod_op_i /= 8u;
                }
            }

            // uses stop bits, i.e. encoded with m_op_mask & OP_STOP_BIT == 1
            enc_operation_index = stop_bits.bv
                                      ? getEncodingIndexWithStopBits(inv_lod, inv_lod_op_i, brick_encoding, stop_bits)
                                      : brick_encoding[inv_lod] + inv_lod_op_i;

            // at this point: inv_lod, and inv_lod_op_i must be valid and set correctly
            assert(inv_lod <= target_inv_lod && "LOD chasing overflow for Huffman Wavelet Matrix decoding.");
            assert(enc_operation_index < wm_header->level_starts_1_to_4[0] && "brick encoding out of bounds read");

            operation = wm_huffman_access(enc_operation_index, wm_header, bit_vector);
            assert((enc_operation_index != 0u || operation == PALETTE_ADV) && "first brick operation must be PALETTE_ADV");
        }

        // at this point, the current operation accesses the palette: write the resulting palette entry
        // the palette index to read is the (exclusive!) rank_{PALETTE_ADV}(enc_operation_index)
        uint32_t palette_index = wm_huffman_rank(enc_operation_index, PALETTE_ADV, wm_header, bit_vector);
        // the actual palette index may be offset depending on the operation
        if (operation == PALETTE_LAST) {
            palette_index--;
        }
        // assert(palette_index < brick_encoding_length[getPaletteSizeHeaderIndex()] && "out of bounds palette index");

        // Write to the index in the output array. The output array's positions are in Morton order.
        return brick_encoding[brick_encoding_length - 1u - palette_index];
    }
}

uint32_t WaveletMatrixEncoder::decompressCSGVBrickVoxel(const uint32_t output_i, const uint32_t target_inv_lod,
                                                        const glm::uvec3 valid_brick_size,
                                                        const uint32_t *brick_encoding,
                                                        const uint32_t brick_encoding_length) const {
    if (m_encoding_mode == WAVELET_MATRIX_ENC) {
        return decompressCSGVBrickVoxelWM(output_i, target_inv_lod, valid_brick_size, brick_encoding,
                                          brick_encoding_length,
                                          getWMBrickHeaderFromEncoding(brick_encoding),
                                          getWMBitVectorFromEncoding(brick_encoding));
    } else if (m_encoding_mode == HUFFMAN_WM_ENC) {
        const FlatRank_BitVector_ptrs stop_bits = (m_op_mask & OP_STOP_BIT)
                                                      ? getWMHStopBitsFromEncoding(brick_encoding,
                                                                                   brick_encoding_length,
                                                                                   brick_encoding[getPaletteSizeHeaderIndex()])
                                                      : FlatRank_BitVector_ptrs(nullptr, nullptr);
        return decompressCSGVBrickVoxelWMHuffman(output_i, target_inv_lod, valid_brick_size, brick_encoding,
                                                 brick_encoding_length,
                                                 getWMHBrickHeaderFromEncoding(brick_encoding),
                                                 getWMHBitVectorFromEncoding(brick_encoding),
                                                 stop_bits);
    } else {
        throw std::runtime_error("Encoding mode not supported by WaveletMatrixEncoder.");
    }
}

void WaveletMatrixEncoder::parallelDecodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                               uint32_t *output_brick, glm::uvec3 valid_brick_size,
                                               const int target_inv_lod) const {
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

    if (m_encoding_mode == WAVELET_MATRIX_ENC) {
        // gather all information required for decoding symbols from a wavelet matrix encoding
        const WMBrickHeader *wm_brick_header = getWMBrickHeaderFromEncoding(brick_encoding);
        const BV_WordType *bit_vector = getWMBitVectorFromEncoding(brick_encoding);

// m_cpu_threads many threads go through the Morton indexing order from front to back. The threads work on the next
// following items in parallel. read_offset is the index of the first thread 0.
//
// Of course, we could directly parallelize over the number of output voxels in a for loop here, but:
// on a GPU m_cpu_threads should be equal to the number of threads in a warp allowing us to do vulkan subgroup optimizations
#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(output_index_step, output_voxel_count, target_inv_lod, brick_encoding, output_brick, target_brick_size, brick_encoding_length, wm_brick_header, bit_vector)
        {
            uint32_t output_i = omp_get_thread_num();
            while (output_i < output_voxel_count) {

                output_brick[output_index_step * output_i] =
                    decompressCSGVBrickVoxelWM(output_i, target_inv_lod, glm::uvec3(m_brick_size),
                                               brick_encoding, brick_encoding_length, wm_brick_header, bit_vector);

                // #pragma omp barrier
                output_i += omp_get_num_threads();
            }
        }
    } else if (m_encoding_mode == HUFFMAN_WM_ENC) {
        // gather all information required for decoding symbols from a wavelet matrix encoding
        const WMHBrickHeader *wm_brick_header = getWMHBrickHeaderFromEncoding(brick_encoding);
        const BV_WordType *bit_vector = getWMHBitVectorFromEncoding(brick_encoding);
        const FlatRank_BitVector_ptrs stop_bits = (m_op_mask & OP_STOP_BIT)
                                                      ? getWMHStopBitsFromEncoding(brick_encoding,
                                                                                   brick_encoding_length,
                                                                                   brick_encoding[getPaletteSizeHeaderIndex()])
                                                      : FlatRank_BitVector_ptrs(nullptr, nullptr);

// m_cpu_threads many threads go through the Morton indexing order from front to back. The threads work on the next
// following items in parallel. read_offset is the index of the first thread 0.
//
// Of course, we could directly parallelize over the number of output voxels in a for loop here, but:
// on a GPU m_cpu_threads should be equal to the number of threads in a warp allowing us to do vulkan subgroup optimizations
#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(output_index_step, output_voxel_count, target_inv_lod, brick_encoding, output_brick, target_brick_size, brick_encoding_length, wm_brick_header, bit_vector, stop_bits)
        {
            uint32_t output_i = omp_get_thread_num();
            while (output_i < output_voxel_count) {

                output_brick[output_index_step * output_i] =
                    decompressCSGVBrickVoxelWMHuffman(output_i, target_inv_lod, glm::uvec3(m_brick_size),
                                                      brick_encoding, brick_encoding_length,
                                                      wm_brick_header, bit_vector, stop_bits);

                // #pragma omp barrier
                output_i += omp_get_num_threads();
            }
        }
    } else {
        throw std::runtime_error("Encoding mode not supported by WaveletMatrixEncoder.");
    }
}

std::vector<std::string>
WaveletMatrixEncoder::getGLSLDefines(const std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                     const uint32_t brick_idx_count) const {
    auto defines = CSGVBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);
    switch (sizeof(BV_WordType)) {
    case 4:
        defines.emplace_back("BV_WORD_TYPE=uint");
        break;
    case 8:
        defines.emplace_back("BV_WORD_TYPE=uint64_t");
        break;
    default:
        throw std::runtime_error("Missing GLSL define for BV_WORD_TYPE");
    }
    defines.emplace_back("HWM_LEVELS=" + std::to_string(HWM_LEVELS));
    defines.emplace_back("BV_L1_BIT_SIZE=" + std::to_string(BV_L1_BIT_SIZE));
    defines.emplace_back("BV_L2_BIT_SIZE=" + std::to_string(BV_L2_BIT_SIZE));
    defines.emplace_back("BV_L2_WORD_SIZE=" + std::to_string(BV_L2_WORD_SIZE));
    defines.emplace_back("BV_STORE_L1_BITS=" + std::to_string(BV_STORE_L1_BITS));
    defines.emplace_back("BV_STORE_L2_BITS=" + std::to_string(BV_STORE_L2_BITS));
    defines.emplace_back("BV_WORD_BIT_SIZE=" + std::to_string(BV_WORD_BIT_SIZE));
    defines.emplace_back("WM_HEADER_INDEX=" + std::to_string(getWMHeaderIndex()));
    defines.emplace_back("UINT_PER_L12=" + std::to_string(sizeof(BV_L12Type) / sizeof(uint32_t)));

    // obtain MAX_BIT_VECTOR_WORD_LENGTH as ceil(max_bitvector_bit_length / BV_WORD_BIT_SIZE)
    uint32_t max_bitvector_bit_length = 0u;
    uint32_t max_brick_encoding_32bit_length = 0u;
#pragma omp parallel for default(none) shared(brick_idx_count, getBrickEncodingSpan) reduction(max : max_bitvector_bit_length)
    for (uint32_t brick_idx = 0u; brick_idx < brick_idx_count; brick_idx++) {
        auto brick_encoding = getBrickEncodingSpan(brick_idx);
        if (m_encoding_mode == HUFFMAN_WM_ENC) {
            const auto wmh_brick_header = getWMHBrickHeaderFromEncoding(brick_encoding.data());
            max_bitvector_bit_length = std::max(max_bitvector_bit_length, wmh_brick_header->bit_vector_size);
        } else {
            const auto wm_brick_header = getWMBrickHeaderFromEncoding(brick_encoding.data());
            max_bitvector_bit_length = std::max(max_bitvector_bit_length, wm_brick_header->text_size * WM_LEVELS);
        }
    }
    defines.emplace_back("MAX_BIT_VECTOR_WORD_LENGTH=" +
                         std::to_string((max_bitvector_bit_length + BV_WORD_BIT_SIZE - 1) / BV_WORD_BIT_SIZE));

    return defines;
}

// DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

void WaveletMatrixEncoder::getBrickStatistics(std::map<std::string, float> &statistics,
                                              const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                              glm::uvec3 valid_brick_size) const {

    // gather header information
    const uint32_t palette_length = brick_encoding[getPaletteSizeHeaderIndex()];
    uint32_t operation_count = 0u;
    uint32_t bit_vector_length = 0u;
    if (m_encoding_mode == WAVELET_MATRIX_ENC) {
        const WMBrickHeader *wm_header = getWMBrickHeaderFromEncoding(brick_encoding);
        operation_count = wm_header->text_size;
        bit_vector_length = operation_count * WM_LEVELS;
    } else if (m_encoding_mode == HUFFMAN_WM_ENC) {
        const WMHBrickHeader *wmh_header = getWMHBrickHeaderFromEncoding(brick_encoding);
        operation_count = wmh_header->level_starts_1_to_4[0];
        bit_vector_length = wmh_header->bit_vector_size;
    } else {
        throw std::runtime_error("encoding mode not supported by Wavelet Matrix encoder");
    }
    const uint32_t bit_vector_words = (bit_vector_length + BV_WORD_BIT_SIZE - 1) / BV_WORD_BIT_SIZE;

    statistics["operation_count"] = static_cast<float>(operation_count);

    statistics["header_byte_size"] = static_cast<float>((getWMHeaderIndex() + 10) * sizeof(uint32_t));
    statistics["operation_stream_byte_size"] = static_cast<float>(sizeof(BV_WordType) * bit_vector_words + (sizeof(BV_L12Type) * getFlatRankEntries(bit_vector_length)));
    double flat_rank_overhead = 0.f;
    size_t stop_bits_byte_size = 0u;
    if (m_op_mask & OP_STOP_BIT) {
        const uint32_t stop_bv_uint_length = brick_encoding[brick_encoding_length - palette_length - 1]; // measured as 32 bit elems
        stop_bits_byte_size += stop_bv_uint_length * sizeof(uint32_t);                                   // stop bit vector
        stop_bits_byte_size += getFlatRankEntries(stop_bv_uint_length * 32u) * sizeof(BV_L12Type);       // stop bit flat rank
        stop_bits_byte_size += sizeof(uint32_t);                                                         // stop bit uint size

        flat_rank_overhead = static_cast<double>(sizeof(BV_L12Type) * getFlatRankEntries(bit_vector_length) + getFlatRankEntries(stop_bv_uint_length * 32u) * sizeof(BV_L12Type));
        flat_rank_overhead /= static_cast<double>(sizeof(BV_WordType) * bit_vector_words + stop_bv_uint_length * sizeof(uint32_t));
    } else {
        flat_rank_overhead = static_cast<float>(sizeof(BV_L12Type) * getFlatRankEntries(bit_vector_length)) / static_cast<float>(sizeof(BV_WordType) * bit_vector_words);
    }
    statistics["stop_bits_byte_size"] = static_cast<float>(stop_bits_byte_size);
    statistics["palette_byte_size"] = static_cast<float>(palette_length * sizeof(uint32_t));
    statistics["flat_rank_overhead"] = static_cast<float>(flat_rank_overhead);
}

void WaveletMatrixEncoder::verifyBrickCompression(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                                  const uint32_t *brick_detail_encoding,
                                                  uint32_t brick_detail_encoding_length,
                                                  std::ostream &error) const {
    // Obtain a reference to the uint buffer containing this bricks encoding.
    const uint32_t minimum_header_size = getWMHeaderIndex() + ((m_encoding_mode == WAVELET_MATRIX_ENC) ? sizeof(WMBrickHeader) : sizeof(WMHBrickHeader)) / 4;
    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_start_lods = lod_count;

    uint32_t total_voxels_in_brick = 0u;
    for (int i = 1; i <= m_brick_size; i <<= 1) {
        total_voxels_in_brick += (i * i * i);
    }

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < minimum_header_size + 1u + 1u) {
        error
            << "brick encoding is shorter than minimum. (header (incl. 1 flatrank) + 1 encoding + 1 palette) = "
            << minimum_header_size + 2u << " but is " << brick_encoding_length << "\n";
    }

    // check LOD starts
    if (brick_encoding[0] != 0) {
        error << "First encoding operation index must be 0." << "\n";
    }
    if (brick_encoding[1] > 1) {
        error << "Second encoding operation index must be 0 or 1." << "\n";
    }

    // Brick headers do no longer store LOD palette starts
    //    // check palette start of first LoD being 0 and second LoD being 1
    //    if (brick_encoding[header_start_lods] != 0u)
    //        error << "  first palette start must be 0 but is " << brick_encoding[header_start_lods] << "\n";
    //    if (brick_encoding[header_start_lods + 1u] != 1u)
    //        error << "  second palette start must be 1 but is " << brick_encoding[header_start_lods + 1u] << "\n";

    if (brick_encoding[getPaletteSizeHeaderIndex()] == 0 || brick_encoding[getPaletteSizeHeaderIndex()] > total_voxels_in_brick)
        error << " palette size must be in [1, total_voxels_in_brick] = [1, " << total_voxels_in_brick
              << " but is " << brick_encoding[getPaletteSizeHeaderIndex()];

    if (m_encoding_mode == WAVELET_MATRIX_ENC) {
        const WMBrickHeader *wm_header = getWMBrickHeaderFromEncoding(brick_encoding);
        if (wm_header->palette_size != brick_encoding[getPaletteSizeHeaderIndex()])
            error << "  wavelet matrix header has palette size " << wm_header->palette_size << " but should be "
                  << brick_encoding[getPaletteSizeHeaderIndex()];
        if (wm_header->text_size == 0u || wm_header->text_size > total_voxels_in_brick)
            error << "  text size must be within (0, " << total_voxels_in_brick << ") but is "
                  << wm_header->text_size << "\n";
        if (getL1Entry(wm_header->fr[0]) != 0)
            error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
        if (getL2Entry(wm_header->fr[0], 0) != 0)
            error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
    } else {
        const WMHBrickHeader *wm_header = getWMHBrickHeaderFromEncoding(brick_encoding);
        // maximum text size: HWM_LEVELS bits per voxel (i.e. 5 bit vectors with length of voxels in brick)
        if (wm_header->bit_vector_size == 0u || wm_header->bit_vector_size > total_voxels_in_brick * HWM_LEVELS)
            error << "  bit vector size must be within (0, " << total_voxels_in_brick * HWM_LEVELS << ") but is "
                  << wm_header->bit_vector_size << "\n";
        if (getL1Entry(wm_header->fr[0]) != 0)
            error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
        if (getL2Entry(wm_header->fr[0], 0) != 0)
            error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
        if (wm_header->ones_before_level[0] != 0u)
            error << "  first ones_before_level entry must be 0 but is " << wm_header->ones_before_level[0] << "\n";
        for (int i = 1; i < 4; i++) {
            if (wm_header->ones_before_level[i] >= wm_header->level_starts_1_to_4[i - 1])
                error << "  ones_before_level[" << i << "] must be < level_starts_1_to_4[" << (i - 1) << "] "
                      << wm_header->level_starts_1_to_4[i - 1] << " but is " << wm_header->ones_before_level[i]
                      << "\n";
        }
        if (wm_header->level_starts_1_to_4[0] > total_voxels_in_brick)
            error << "  level_starts_1_to_4[0] entry must be the text size, limited by voxel count "
                  << total_voxels_in_brick << " but is " << wm_header->level_starts_1_to_4[0] << "\n";
        for (int i = 0; i < 3; i++) {
            if (wm_header->level_starts_1_to_4[i] > wm_header->level_starts_1_to_4[i + 1])
                error << " level_starts_1_to_4[" << i << "] entry must be <= level_starts_1_to_4[" << (i + 1)
                      << "] = " << wm_header->level_starts_1_to_4[i + 1] << " but is "
                      << wm_header->level_starts_1_to_4[i] << "\n";
        }
        if (wm_header->level_starts_1_to_4[3] > wm_header->bit_vector_size)
            error << " level_starts_1_to_4[3] entry must be <= bit_vector_size " << wm_header->bit_vector_size
                  << " but is " << wm_header->level_starts_1_to_4[3] << "\n";
    }
}

std::string WaveletMatrixEncoder::outputOperationStream(const std::span<const uint32_t> encoding,
                                                        const uint32_t offset, const uint32_t count) const {
    const WMHBrickHeader *wm_header = getWMHBrickHeaderFromEncoding(encoding.data());
    const BV_WordType *bit_vector = getWMHBitVectorFromEncoding(encoding.data());

    std::stringstream ss;
    for (uint32_t i = 0; i < count; i++)
        ss << wm_huffman_access(offset + i, wm_header, bit_vector) << ", ";
    return ss.str();
}

// COMONENT ACCESS

// Wavelet Matrix -----

inline const WMBrickHeader *WaveletMatrixEncoder::getWMBrickHeaderFromEncoding(const uint32_t *v) const {
    // to ensure tight packing, the WMBrickHeader starts one uint earlier in the previous (normal) brick header
    return reinterpret_cast<const WMBrickHeader *>(v + getWMHeaderIndex());
}

inline const BV_WordType *WaveletMatrixEncoder::getWMBitVectorFromEncoding(const uint32_t *v) const {
    return reinterpret_cast<const BV_WordType *>(v + getWMHeaderIndex() + 10 + (sizeof(BV_L12Type) / sizeof(uint32_t)) * getFlatRankEntries(v[getWMHeaderIndex() + 1] * WM_LEVELS));
}

// Huffman Wavelet Matrix ----

inline const WMHBrickHeader *WaveletMatrixEncoder::getWMHBrickHeaderFromEncoding(const uint32_t *v) const {
    return reinterpret_cast<const WMHBrickHeader *>(v + getWMHeaderIndex());
}

inline const BV_L12Type *WaveletMatrixEncoder::getWMHFlatRankFromEncoding(const uint32_t *v) const {
    return reinterpret_cast<const BV_L12Type *>(v + getWMHeaderIndex() + 10);
}

inline const BV_WordType *WaveletMatrixEncoder::getWMHBitVectorFromEncoding(const uint32_t *v) const {
    return reinterpret_cast<const BV_WordType *>(v + getWMHeaderIndex() + 10 + (sizeof(BV_L12Type) / sizeof(uint32_t)) * getFlatRankEntries(v[getWMHeaderIndex()]));
}

inline FlatRank_BitVector_ptrs WaveletMatrixEncoder::getWMHStopBitsFromEncoding(const uint32_t *brick_encoding,
                                                                                const uint32_t brick_encoding_length,
                                                                                const uint32_t palette_size) {
    const uint32_t stop_bv_length = brick_encoding[brick_encoding_length - palette_size - 1];
    assert(palette_size + 1 + stop_bv_length < brick_encoding_length);

    FlatRank_BitVector_ptrs stop_bits = {};
    stop_bits.bv = reinterpret_cast<const BV_WordType *>(brick_encoding + brick_encoding_length - palette_size - 1 - stop_bv_length);
    stop_bits.fr = (stop_bits.bv - getFlatRankEntries(stop_bv_length * 32) * (sizeof(BV_L12Type) / sizeof(BV_WordType)));

    assert(getL1Entry(stop_bits.fr[0]) == 0u && "corrupted stop bit flat rank pointer: first L1 is not 0");
    return stop_bits;
}

} // namespace volcanite
