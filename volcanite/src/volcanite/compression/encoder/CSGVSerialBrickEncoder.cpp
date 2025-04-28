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

#include "volcanite/compression/encoder/CSGVSerialBrickEncoder.hpp"
#include "volcanite/compression/VolumeCompressionBase.hpp"
#include "volcanite/compression/memory_mapping.hpp"
#include "volcanite/compression/pack_nibble.hpp"

#include "csgv_constants.incl"
#include "vvv/util/util.hpp"

namespace volcanite {

void printBrick(const std::vector<uint32_t> &brick, uint32_t brick_size, int z_step, vvv::loglevel log) {
    static const std::string digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                                         "A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    std::string s;
    for (int i = static_cast<int>(brick_size) - 1; i >= -1; i--) {
        for (int z = 0; z < brick_size; z += z_step) {
            s += (i < 0) ? digits[z % 20] + "|" : digits[i % 20] + " ";
            for (int n = 0; n < brick_size; n++) {
                if (i < 0) {
                    s += digits[n % 20] + " ";
                    continue;
                }
                auto v = brick[sfc::Morton3D::p2i(glm::uvec3(n, i, z))];
                if (v == INVALID)
                    s += " ";
                else
                    s += std::to_string(v % 10);
                s += " ";
            }

            s += "   ";
        }
        Logger(log) << s;
        s = "";
    }
}

uint32_t CSGVSerialBrickEncoder::valueOfNeighbor(const uint32_t *brick, const glm::uvec3 &brick_pos,
                                                 const uint32_t child_index, const uint32_t lod_width,
                                                 const uint32_t brick_size, const int neighbor_i) {
    assert(lod_width > 0);
    assert(child_index < 8);
    // find the position of the neighbor
    glm::ivec3 neighbor_pos = glm::ivec3(brick_pos) + neighbor[child_index][neighbor_i] * static_cast<int>(lod_width);
    if (glm::any(glm::lessThan(neighbor_pos, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(neighbor_pos, glm::ivec3(static_cast<int>(brick_size))))) {
        // this is only called during decompression in which case nothing outside the volume should be referenced
        assert(false && "Invalid neighbor reference!");
        return INVALID;
    }
    // find the index of the neighbor within the brick array
    uint32_t neighbor_index = indexOfBrickPos(glm::uvec3(neighbor_pos));

    // in case we want to access a neighbor that is not already existing on this level
    // (neighbor_i > our_i or any element of neighbor[child_index][neighbor_i] is positive), we have to
    // round down to the parent element of this element (lod_width*8)
    if (glm::any(glm::greaterThan(neighbor[child_index][neighbor_i], glm::ivec3(0))))
        neighbor_index -= neighbor_index % (lod_width * lod_width * lod_width * 8);

    // since we don't check here if we're out of bounds of the volume, it CAN happen that a value is INVALID in the encoding
    // in the decoding, such a neighbor should never be accessed
    assert(brick[neighbor_index] != INVALID && "Trying to access a neighbor that was not yet set!");

    // return value of neighbor or parent neighbor in brick
    return brick[neighbor_index];
}

void CSGVSerialBrickEncoder::verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                                    const uint32_t *detail_encoding, uint32_t detail_encoding_length,
                                                    std::ostream &error) const {

    uint32_t header_size = getHeaderSize();
    uint32_t lod_count = getLodCountPerBrick();
    uint32_t header_start_lods = lod_count - (detail_encoding ? 1 : 0);

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < header_size + 1u + 1u)
        error << " brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette)=" << (header_size + 2) << " but is " << brick_encoding_length << "\n";

    // check first header entry being header_size * 8
    if (brick_encoding[0] != header_size * 8u)
        error << "  first encoding starts 4bit must be header*8=" << (header_size * 8u) << " but is " << brick_encoding[0] << "\n";

    // check encoding starts being in ascending order
    // note: the header count the number of entries, except the last entry when using double table rANS
    // for which this entry refers to the raw 4 bit index at which the detail encoding starts AFTER packing the earlier LoDs
    for (int l = 1; l < header_start_lods - (m_encoding_mode == DOUBLE_TABLE_RANS_ENC ? 1 : 0); l++) {
        long distance = static_cast<long>(brick_encoding[l]) - static_cast<long>(brick_encoding[l - 1]);
        if (distance < 0l) {
            error << "  encoding starts are not in ascending order (distance " << distance << " for LoD " << l << ")\n";
            break;
        } else if (distance > m_brick_size * m_brick_size * m_brick_size) {
            error << "  encoding starts between LoDs are too far away\n";
            break;
        }
    }

    // Brick headers do no longer store LOD palette starts
    //    // check palette start of first LoD being 0 and second LoD being 1
    //    if(brick_encoding[header_start_lods] != 0u)
    //        error << "  first palette start must be 0 but is " << brick_encoding[header_start_lods] << "\n";
    //    if(brick_encoding[header_start_lods + 1u] != 1u)
    //        error << "  second palette start must be 1 but is " << brick_encoding[header_start_lods + 1u] << "\n";
    //    // check palette starts being in ascending order
    //    for(int l = 2u; l <= lod_count + 1; l++) {
    //        if(brick_encoding[header_start_lods + l] < brick_encoding[header_start_lods + l - 1]) {
    //            error << "  palette starts are not in ascending order\n";
    //            break;
    //        }
    //    }

    uint32_t palette_size = brick_encoding[getPaletteSizeHeaderIndex()];
    // check palette size not being zero
    if (palette_size == 0u) {
        error << "  palette size is zero\n";
    }

    // check palette size + encoding start of last LoD being shorter than the brick encoding
    if (palette_size + brick_encoding[header_start_lods] / 8u > brick_encoding_length) {
        error << "  palette size and encoding of first (L-1) levels are longer than the total brick encoding\n";
    }

    // check detail encoding having at least 1 entry
    if (m_separate_detail) {
        if (detail_encoding_length < 1l) {
            error << "  brick detail encoding is missing with length " << detail_encoding_length << "\n";
        }
    }
}

// BRICK MEMORY LAYOUT for L = log2(brick_size) LODs
// HEADER                 ENCODING:
// 4bit_encoding_start[0, 1, .. L-1], palette_start[0, 1 .. L], 4bit_encoding_padded_to32bit[0, 1, .. L], 32bit_palette[L, .., 1, 0]
//       header_size*8 ᒧ                always zero ᒧ  ∟ .. one  ∟ palette size
uint32_t CSGVSerialBrickEncoder::encodeBrick(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out, const glm::uvec3 start, const glm::uvec3 volume_dim) const {
    assert(m_encoding_mode == NIBBLE_ENC || m_rans_initialized);

    std::vector<uint32_t> palette;
    glm::uvec3 volume_pos, brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();
    const uint32_t header_size = getHeaderSize();
    uint32_t out_i = header_size * 8u; // write head position in out, counted as number of encoded 4 bit elements

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index; // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    VolumeCompressionBase::constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size,
                                              m_op_mask & OP_STOP_BIT, false);

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
        // out[lod_count + current_inv_lod] = static_cast<uint32_t>(palette.size()); (not writing LOD palette sizes anymore)

        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size / lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        bool in_detail_lod = (m_encoding_mode == DOUBLE_TABLE_RANS_ENC) && (current_inv_lod == lod_count - 1u);

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for a grid node that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write STOP_BIT.
            brick_pos = enumBrickPos(i);
            volume_pos = start + brick_pos;
            if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
            if (child_index == 0) {
                assert(parent_counter <= 8 && "parent element would be used for more than 8 elements!");

                // if this subtree is already filled (because in a previous LOD we set a STOP_BIT for this area), the last element of this block is set, and we can skip it
                // note that this will also happen if this grid node lies completely outside the volume because some parent would've been set to STOP_BIT earlier
                // our parent spanned 8 elements of this finer current level, so we need to look at the element 7 indices further
                if (multigrid[parent_multigrid_lod_start +
                              voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))]
                        .constant_subregion) {
                    parent_counter = 0;
                    i += (lod_width * lod_width * lod_width * 7);
                    continue;
                }

                parent_counter = 0;
                parent_value = multigrid[parent_multigrid_lod_start +
                                         voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))]
                                   .label;
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
            if ((m_op_mask & OP_PARENT_BIT) && value == parent_value)
                operation |= PARENT;
            else if ((m_op_mask & OP_NEIGHBORX_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if ((m_op_mask & OP_NEIGHBORY_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if ((m_op_mask & OP_NEIGHBORZ_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if ((m_op_mask & OP_PALETTE_LAST_BIT) && palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                // reuse the n-DELTA palette value where 0 < DELTA
                uint32_t palette_delta = static_cast<uint32_t>(std::find(palette.rbegin(), palette.rend(), value) - palette.rbegin());
                if ((m_op_mask & OP_PALETTE_D_BIT) && palette_delta < palette.size() && palette_delta <= MAX_PALETTE_DELTA_DISTANCE) {
                    assert(palette.at(palette.size() - palette_delta - 1u) == value && "palette label does not fit for delta");
                    assert(palette_delta > 0u && "palette delta 0 should've been caught by the palette_last value!");

                    if (m_op_mask & OP_USE_OLD_PAL_D_BIT) {
                        // the old mode stores only one single 4 bit element for the delta: 0 < palette_delta < 17u
                        if (palette_delta < 17u) {
                            write4Bit(out, 0u, out_i++, operation | PALETTE_D);
                            // "0" case is already handled by PALETTE_LAST, so we only consider case 1 to 16 in 4 bits
                            operation = palette_delta - 1u;
                        } else {
                            // otherwise, add a new palette entry
                            palette.push_back(value);
                            operation |= PALETTE_ADV;
                        }
                    } else {
                        palette_delta--;                                                     // the "0" case is already handled by PALETTE_LAST. Only consider cases 1 ... MAX_PALETTE_DELTA_DISTANCE
                        int palette_delta_shift = (glm::findMSB(palette_delta) / 3 + 1) * 3; // start one after the MSB 3 bit package
                        // the operation stream will consist of
                        // [PALETTE_D | STOP_BIT] [CONTINUE_DELTA_BIT][DELTA 1st 3 MSB] [CONTINUE_DELTA_BIT][DELTA 2nd 3 MSB] ...
                        operation |= PALETTE_D;
                        do {
                            write4Bit(out, 0u, out_i++, operation);
                            palette_delta_shift -= 3;                                // move over to next three bits
                            operation = (palette_delta >> palette_delta_shift) & 7u; // write the next 3 most-significant bits of delta
                            operation |= (palette_delta_shift > 0u ? 8u : 0u);       // set the 4th MSB of this entry if delta has bits remaining
                        } while (palette_delta_shift > 0u);
                    }
                } else { // if nothing helps, add a completely new palette entry
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }

            assert(operation < 16u && "only 4 bit operations are allowed");
            write4Bit(out, 0u, out_i++, operation);

            assert(value != INVALID);
        }

        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            // pack all previous levels via rANS encoding if we're at the second last LoD (last LoD of non-detail encoding)
            // NOTE: the old out_i and header starts count in number of elements. the following out_i counts in 4bit
            if (current_inv_lod == lod_count - 2u) {
                out_i = m_rans.packRANS(out, out[0], out_i);
                // the detail encoding has to start at a new 32bit element (which is guaranteed by our rANS output)
                assert(out_i % 8u == 0u && "next element after rANS output should start at a new uint32_t element");
            }
            // pack the detail (=finest LOD) via rANS encoding.
            // We have a separate rANS encoder here because the detail level does not use stop bits => different operation frequencies
            else if (in_detail_lod) {
                out_i = m_detail_rans.packRANS(out, out[current_inv_lod], out_i);
            }
        }
        current_inv_lod++;
    }

    // if we did not apply the rANS packing before, because we are only using a single freq. table, we do it here
    if (m_encoding_mode == SINGLE_TABLE_RANS_ENC)
        out_i = m_rans.packRANS(out, out[0], out_i);

    // last entry of our header stores the palette size
    out[getPaletteSizeHeaderIndex()] = palette.size();
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
    return out_i; // we return the number of uint32_t elements that we used
}

void CSGVSerialBrickEncoder::decodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                         const uint32_t *brick_detail_encoding,
                                         const uint32_t brick_detail_encoding_length,
                                         uint32_t *output_brick, glm::uvec3 valid_brick_size, int inv_lod) const {
    assert(m_encoding_mode == NIBBLE_ENC || m_rans_initialized);

    // the palette starts at the end of the encoding block
    uint32_t paletteE = brick_encoding_length - 1u;
    const uint32_t *brick_palette = brick_encoding;

    // first: read the header (= first header entry is the start positions of the inv. LoD 0)
    uint32_t lod_count = getLodCountPerBrick();
    ReadState readState = {.idxE = brick_encoding[0], .in_detail_lod = false};
    if (m_encoding_mode != NIBBLE_ENC) {
        // idxE counts in bytes for rANS state instead of number of 4 bit entries
        readState.idxE = (readState.idxE / 8) * 4;
        m_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
    }

    uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
    uint32_t lod_width = m_brick_size;
    uint32_t parent_value = INVALID;
    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++)
        output_brick[i] = INVALID;

    for (int lod = 0; lod <= inv_lod; lod++) {

        // check if we ran into the detail layer and change the readState accordingly
        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC && lod == lod_count - 1) {
            readState.in_detail_lod = true;
            if (m_separate_detail) {
                // we now read from the separated detail encoding buffer
                brick_encoding = brick_detail_encoding;
                readState.idxE = 0u;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            } else {
                // Read the lod start from the brick header to start reading at the right encoding buffer index.
                // We have to start at a fully padded uint32, because we switch the rANS decoder.
                readState.idxE = (brick_encoding[lod] / 8) * 4;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            }
        }

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
            // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
            if (glm::any(glm::greaterThanEqual(enumBrickPos(i), valid_brick_size)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            uint32_t child_index = (i % (index_step * 8)) / index_step;
            if (lod > 0 && i % (index_step * 8) == 0) {

                // if this subtree is already filled (because in a previous LOD we had a STOP_BIT for this area), the last element of this block is set and we can skip it
                if (output_brick[i + (index_step * 7)] != INVALID) {
                    i += (index_step * 7);
                    continue;
                }

                parent_value = output_brick[i];
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }

            // get the next operation and apply it (either progress in the current RLE or read the next entry)
            uint32_t operation = readNextLodOperationFromEncoding(brick_encoding, readState);

            uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits
            if (operation_lsb == PARENT)
                output_brick[i] = parent_value;
            else if (operation_lsb == NEIGHBOR_X)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 0);
            else if (operation_lsb == NEIGHBOR_Y)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 1);
            else if (operation_lsb == NEIGHBOR_Z)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 2);
            else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
                output_brick[i] = brick_palette[paletteE--];
            } else if (operation_lsb == PALETTE_LAST) {
                output_brick[i] = brick_palette[paletteE + 1];
            } else if (operation_lsb == PALETTE_D) {
                uint32_t palette_delta = 0u;
                if (m_op_mask & OP_USE_OLD_PAL_D_BIT) {
                    palette_delta = readNextLodOperationFromEncoding(brick_encoding, readState);
                } else {
                    while (true) {
                        const uint32_t next_delta_bits = readNextLodOperationFromEncoding(brick_encoding, readState);
                        // 3 LSB are the next three bits of the
                        palette_delta = (palette_delta << 3u) | (next_delta_bits & 7u);
                        if ((next_delta_bits & 8u) == 0u)
                            break;
                    }
                }
                output_brick[i] = brick_palette[paletteE + palette_delta + 2u];
            } else
                assert(false && "unrecognized compression operation");

            // stop traversal: fill all other parts of the brick with this value
            if ((operation & STOP_BIT) > 0u) {
                // fill the whole subtree with the parent value
                for (uint32_t n = i; n < i + index_step; n++) {
                    output_brick[n] = output_brick[i];
                }
            }

            assert(output_brick[i] != INVALID && "Set output element brick to forbidden magic value INVALID!");
        }

        // move to the next LOD block with half the block width and an eight of the index_step respectively
        index_step /= 8;
        lod_width /= 2;
    }
}

void CSGVSerialBrickEncoder::decodeBrickWithDebugEncoding(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                                          const uint32_t *brick_detail_encoding,
                                                          const uint32_t brick_detail_encoding_length,
                                                          uint32_t *output_brick, uint32_t *output_encoding,
                                                          std::vector<glm::uvec4> *output_palette, glm::uvec3 valid_brick_size,
                                                          int inv_lod) const {
    assert(m_encoding_mode == NIBBLE_ENC || m_rans_initialized);

    // the palette starts at the end of the encoding block
    uint32_t paletteE = brick_encoding_length - 1u;
    const uint32_t *brick_palette = brick_encoding;

    // first: read the header (= first header entry is the start positions of the inv. LoD 0)
    uint32_t lod_count = getLodCountPerBrick();
    ReadState readState = {.idxE = brick_encoding[0], .in_detail_lod = false};
    if (m_encoding_mode != NIBBLE_ENC) {
        // idxE counts in bytes for rANS state instead of number of 4 bit entries
        readState.idxE = (readState.idxE / 8) * 4;
        m_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
    }

    uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
    uint32_t lod_width = m_brick_size;
    uint32_t parent_value = INVALID;
    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
        output_brick[i] = INVALID;
        output_encoding[i] = INVALID;
    }
    if (output_palette) {
        output_palette->resize(inv_lod + 2, glm::uvec4(0u));
    }
    std::map<uint32_t, uint32_t> output_palette_duplicates;

    for (int lod = 0; lod <= inv_lod; lod++) {

        if (output_palette)
            output_palette->at(lod) = glm::uvec4(output_palette->size());

        // check if we ran into the detail layer and change the readState accordingly
        if (m_encoding_mode == DOUBLE_TABLE_RANS_ENC && lod == lod_count - 1) {
            readState.in_detail_lod = true;
            if (m_separate_detail) {
                // we now read from the separated detail encoding buffer
                brick_encoding = brick_detail_encoding;
                readState.idxE = 0u;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            } else {
                // Read the lod start from the brick header to start reading at the right encoding buffer index.
                // We have to start at a fully padded uint32, because we switch the rANS decoder.
                readState.idxE = (brick_encoding[lod] / 8) * 4;
                m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, brick_encoding);
            }
        }

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
            // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
            if (glm::any(glm::greaterThanEqual(enumBrickPos(i), valid_brick_size)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            uint32_t child_index = (i % (index_step * 8)) / index_step;
            if (lod > 0 && i % (index_step * 8) == 0) {

                // if this subtree is already filled (because in a previous LOD we had a STOP_BIT for this area), the last element of this block is set and we can skip it
                if (output_brick[i + (index_step * 7)] != INVALID) {
                    output_encoding[i] = INVALID;
                    i += (index_step * 7);
                    continue;
                }

                parent_value = output_brick[i];
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }

            // get the next operation and apply it (either progress in the current RLE or read the next entry)
            uint32_t operation = readNextLodOperationFromEncoding(brick_encoding, readState);
            output_encoding[i] = operation;

            uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits
            if (operation_lsb == PARENT)
                output_brick[i] = parent_value;
            else if (operation_lsb == NEIGHBOR_X)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 0);
            else if (operation_lsb == NEIGHBOR_Y)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 1);
            else if (operation_lsb == NEIGHBOR_Z)
                output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i), child_index, lod_width, m_brick_size, 2);
            else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
                output_brick[i] = brick_palette[paletteE--];
                if (output_palette) {
                    auto value = output_brick[i];
                    if (!output_palette_duplicates.contains(value)) {
                        output_palette_duplicates[value] = 0u;
                    }
                    output_palette->push_back(glm::uvec4{value, lod, i, output_palette_duplicates[value]});
                    output_palette_duplicates[value]++;
                }
            } else if (operation_lsb == PALETTE_LAST) {
                output_brick[i] = brick_palette[paletteE + 1];
            } else if (operation_lsb == PALETTE_D) {
                uint32_t palette_delta = 0u;
                if (m_op_mask & OP_USE_OLD_PAL_D_BIT) {
                    palette_delta = readNextLodOperationFromEncoding(brick_encoding, readState);
                } else {
                    while (true) {
                        uint32_t next_delta_bits = readNextLodOperationFromEncoding(brick_encoding, readState);
                        // 3 LSB are the next three bits of the
                        palette_delta = (palette_delta << 3u) | (next_delta_bits & 7u);
                        if ((next_delta_bits & 8u) == 0u)
                            break;
                    }
                }
                output_brick[i] = brick_palette[paletteE + palette_delta + 2u];
            } else
                assert(false && "unrecognized compression operation");

            // stop traversal: fill all other parts of the brick with this value
            if ((operation & STOP_BIT) > 0u) {
                // fill the whole subtree with the parent value
                for (uint32_t n = i; n < i + index_step; n++) {
                    output_brick[n] = output_brick[i];
                }
            }

            assert(output_brick[i] != INVALID && "Set output element brick to forbidden magic value INVALID!");
        }

        // move to the next LOD block with half the block width and an eight of the index_step respectively
        index_step /= 8;
        lod_width /= 2;
    }

    // last dummy size element for palette lod starts
    if (output_palette)
        output_palette->at(inv_lod + 1) = glm::uvec4(output_palette->size());
}

void CSGVSerialBrickEncoder::freqEncodeBrick(const std::vector<uint32_t> &volume, size_t *brick_freq, glm::uvec3 start,
                                             glm::uvec3 volume_dim, bool detail_freq) const {
    std::vector<uint32_t> palette;
    palette.reserve(32);
    glm::uvec3 volume_pos, brick_pos;

    const uint32_t lod_count = getLodCountPerBrick();

    // we need to keep track of the current brick status from coarsest to finest level to determine the right operations
    // basically do an implicit decoding while we're encoding
    uint32_t parent_value;
    uint32_t value;
    uint32_t child_index; // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

    // construct the multigrid on this brick that we want to represent in this encoding
    std::vector<MultiGridNode> multigrid;
    VolumeCompressionBase::constructMultiGrid(multigrid, volume, volume_dim, start, m_brick_size, m_op_mask & OP_STOP_BIT,
                                              false);

    // we start with the coarsest LOD, which is always a PALETTE_ADV of the max occuring value in the whole brick
    // we handle this here because it allows us to skip some special handling (for example checking if the palette is empty) in the following loop
    // in theory, we could start with a finer level here too and skip the first levels (= Carsten's original idea)
    uint32_t muligrid_lod_start = multigrid.size() - 1;
    if (multigrid[muligrid_lod_start].constant_subregion) {
        brick_freq[PALETTE_ADV | STOP_BIT]++;
    } else {
        brick_freq[PALETTE_ADV]++;
    }
    palette.push_back(multigrid[muligrid_lod_start].label);

    // now we iteratively refine from coarse (8 elements in the brick) to finest (brick_size^3 elements in the brick) levels
    uint32_t current_inv_lod = 1u;
    for (uint32_t lod_width = m_brick_size / 2u; lod_width > 0u; lod_width /= 2u) {
        // in the multigrid, LODs are ordered from finest to coarsest, so we have to go through them in reverse.
        uint32_t lod_dim = (m_brick_size / lod_width);
        uint32_t parent_multigrid_lod_start = muligrid_lod_start;
        muligrid_lod_start -= lod_dim * lod_dim * lod_dim;

        int current_lod_palette = 0;

        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += lod_width * lod_width * lod_width) {
            // we don't store any operations for grid nodes that would lie completely outside the volume
            // if this is problematic, and we would like to always handle a full brick, we could output anything here and thus just write STOP_BIT.
            brick_pos = enumBrickPos(i);
            volume_pos = start + brick_pos;
            if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim)))
                continue;

            // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
            child_index = (i % (lod_width * lod_width * lod_width * 8)) / (lod_width * lod_width * lod_width);
            if (child_index == 0) {
                // if this subtree is already filled (because in a previous LOD we set a STOP_BIT for this area), the last element of this block is set and we can skip it
                // note that this will also happen if this LOD block lies completely outside the volume because some parent would've been set to STOP_BIT earlier
                // our parent spanned 8 elements of this finer current level, so we need to look at the element 7 indices further
                if ((m_op_mask & OP_STOP_BIT) && multigrid[parent_multigrid_lod_start +
                                                           voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))]
                                                     .constant_subregion) {
                    i += (lod_width * lod_width * lod_width * 7);
                    continue;
                }
                parent_value = multigrid[parent_multigrid_lod_start +
                                         voxel_pos2idx(brick_pos / lod_width / 2u, glm::uvec3(lod_dim / 2u))]
                                   .label;
                assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
            }

            value = multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].label;
            assert(value != INVALID && "Original volume mustn't contain the INVALID magic value!");

            uint32_t operation = 0u;
            // if the whole subtree from here has this parent_value, we can set a stop sign and fill the whole brick area of the subtree
            // note that grid nodes outside the volume are by definition also homogeneous
            if ((m_op_mask & OP_STOP_BIT) && lod_width >= 1 && multigrid[muligrid_lod_start + voxel_pos2idx(brick_pos / lod_width, glm::uvec3(lod_dim))].constant_subregion) {
                operation = STOP_BIT;
            }
            // determine operation for the next entry
            [[likely]]
            if ((m_op_mask & OP_PARENT_BIT) && value == parent_value)
                operation |= PARENT;
            else if ((m_op_mask & OP_NEIGHBORX_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 0) == value)
                operation |= NEIGHBOR_X;
            else if ((m_op_mask & OP_NEIGHBORY_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 1) == value)
                operation |= NEIGHBOR_Y;
            else if ((m_op_mask & OP_NEIGHBORZ_BIT) && valueOfNeighbor(multigrid.data() + muligrid_lod_start, multigrid.data() + parent_multigrid_lod_start, brick_pos / lod_width, child_index, lod_dim, m_brick_size, 2) == value)
                operation |= NEIGHBOR_Z;
            else if ((m_op_mask & OP_PALETTE_LAST_BIT) && palette.back() == value)
                operation |= PALETTE_LAST;
            else {
                uint32_t palette_delta = static_cast<uint32_t>(std::find(palette.rbegin(), palette.rend(), value) - palette.rbegin());
                if ((m_op_mask & OP_PALETTE_D_BIT) && palette_delta < palette.size() && palette_delta <= MAX_PALETTE_DELTA_DISTANCE) {
                    assert(palette.at(palette.size() - palette_delta - 1u) == value && "palette label does not fit for delta");
                    assert(palette_delta > 0u && "palette delta 0 should've been caught by the palette_last value!");

                    if (m_op_mask & OP_USE_OLD_PAL_D_BIT) {
                        // the old mode stores only one single 4 bit element for the delta: 0 < palette_delta < 17u
                        if (palette_delta < 17u) {
                            if (detail_freq && (current_inv_lod == lod_count - 1u))
                                brick_freq[16 + (operation | PALETTE_D)]++;
                            else
                                brick_freq[(operation | PALETTE_D)]++;
                            // "0" case is already handled by PALETTE_LAST, so we only consider case 1 to 16 in 4 bits
                            operation = palette_delta - 1u;
                        } else {
                            // otherwise, add a new palette entry
                            palette.push_back(value);
                            operation |= PALETTE_ADV;
                        }
                    } else {
                        palette_delta--;                                                     // the "0" case is already handled by PALETTE_LAST. Only consider cases 1 ... MAX_PALETTE_DELTA_DISTANCE
                        int palette_delta_shift = (glm::findMSB(palette_delta) / 3 + 1) * 3; // start one after the MSB 3 bit package
                        // the operation stream will consist of
                        // [PALETTE_D | STOP_BIT] [CONTINUE_DELTA_BIT][DELTA 1st 3 MSB] [CONTINUE_DELTA_BIT][DELTA 2nd 3 MSB] ...
                        operation |= PALETTE_D;
                        do {
                            if (detail_freq && (current_inv_lod == lod_count - 1u))
                                brick_freq[16 + operation]++;
                            else
                                brick_freq[operation]++;
                            palette_delta_shift -= 3;                                // move over to next three bits
                            operation = (palette_delta >> palette_delta_shift) & 7u; // write the next 3 most-significant bits of delta
                            operation |= (palette_delta_shift > 0u ? 8u : 0u);       // set the 4th MSB of this entry if delta has bits remaining
                        } while (palette_delta_shift > 0u);
                    }
                } else { // if nothing helps, we add a completely new palette entry
                    current_lod_palette++;
                    palette.push_back(value);
                    operation |= PALETTE_ADV;
                }
            }
            assert(operation < 16u && "we only allow writing 4 bit operations!");
            if (detail_freq && (current_inv_lod == lod_count - 1u))
                brick_freq[16 + operation]++;
            else
                brick_freq[operation]++;

            assert(value != INVALID);
        }
        current_inv_lod++;
    }
}

} // namespace volcanite
