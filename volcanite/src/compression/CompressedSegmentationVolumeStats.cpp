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

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include <map>

#include "volcanite/compression/pack_nibble.hpp"

using namespace vvv;

namespace volcanite {

void CompressedSegmentationVolume::printBrickInfo(glm::uvec3 brick, loglevel log_level) {
    Logger(log_level) << "printBrickInfo(..) not adapted for CSGVBrickEncoder yet.";
    //        if(m_encodings.empty())
    //            throw std::runtime_error("Segmentation volume is not yet compressed!");
    //
    //        uint32_t brick_idx = brick_pos2idx(brick, getBrickCount());
    //        const uint32_t* encoding = getBrickEncoding(brick_idx);
    //
    //        uint32_t start = getBrickStart(brick_idx);
    //        uint32_t p = 0;
    //        std::stringstream ss;
    //        ss << "Brick [enc. " << brick_idx / m_brick_idx_to_enc_vector << "] " <<  str(brick) << " "
    //            << getLodCountPerBrick() << "xLoD [Header @" << start << "] LoD Starts: ";
    //
    //        if(isUsingSeparateDetail()) {
    //            for(int i = 0; i < getLodCountPerBrick() - 1; i++) {
    //                ss << std::to_string(encoding[p++]);
    //                if(i < getLodCountPerBrick() - 2)
    //                    ss << ",";
    //            }
    //            ss << " | LoD Palette Start: ";
    //            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
    //                ss << std::to_string(encoding[p++]);
    //                if(i < getLodCountPerBrick())
    //                    ss << ",";
    //            }
    //            ss << " (header size " << p << ") ";
    //            ss << " [Encoding @" << (start + p) << "] ";
    //            for(int i = 0; i < std::min(8u, encoding[getLodCountPerBrick() - 2]); i++) {
    //                ss << encoding[p++] << ",";
    //            }
    //            start = getBrickDetailStart(brick_idx);
    //            encoding = getBrickDetailEncoding(brick_idx);
    //            p = 0;
    //            ss << ".. [Detail @" << start << "] ";
    //            for(int i = 0; i < std::min(8u, getBrickDetailEncodingLength(brick_idx)); i++) {
    //                ss << encoding[p++] << ",";
    //            }
    //            ss << "..";
    //        }
    //        else {
    //            for(int i = 0; i < getLodCountPerBrick(); i++) {
    //                ss << std::to_string(encoding[p]);
    //                if(i < getLodCountPerBrick() - 1)
    //                    ss << ",";
    //                p++;
    //            }
    //            ss << " | LoD Palette Size: ";
    //            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
    //                ss << std::to_string(encoding[p]);
    //                if(i < getLodCountPerBrick())
    //                    ss << ",";
    //                p++;
    //            }
    //            ss << " (header size " << p << ") ";
    //            ss << " [Encoding @" << (p + start) << "] ";
    //            for(int i = 0; i < std::min(8u, encoding[getLodCountPerBrick() - 2]); i++) {
    //                ss << encoding[p++] << ",";
    //            }
    //        }
    //        Logger(log_level) << ss.str();
}

void CompressedSegmentationVolume::printBrickEncoding(uint32_t brick_idx) const {
    throw std::runtime_error("not adapted for CSGVBrickEncoding refactor.");
    //        if (m_encoding_mode != NIBBLE_ENC)
    //            throw std::runtime_error("Can only print brick encoding in NIBBLE_ENC mode.");
    //        if (!m_random_access)
    //            throw std::runtime_error("Can only print brick encoding with random access.");
    //
    //        const uint32_t* brick_encoding = getBrickEncoding(brick_idx);
    //        const uint32_t l = getBrickEncodingLength(brick_idx);
    //
    //        std::stringstream ss;
    //        ss << "Brick " << brick_idx << " operation stream:\n";
    //
    //        const uint32_t ops_per_line = 64;
    //
    //        uint32_t i = brick_encoding[0];
    //        uint32_t voxels_in_inv_lod = 1u;
    //        uint32_t op_count[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
    //        char op_char[7] = {'.', 'x', 'y', 'z', 'A', 'L', 'D'};
    //        for (int inv_lod = 0; inv_lod < getLodCountPerBrick(); inv_lod++) {
    //            ss << "[" << inv_lod << "] ";
    //            for (int v = 0; v < voxels_in_inv_lod; v++) {
    //                uint32_t op = read4Bit(brick_encoding, 0u, i++);
    //                if (op < 7u) {
    //                    op_count[op]++;
    //                    ss << op_char[op];
    //                } else {
    //                    ss << "#";
    //                }
    //
    //                if (v % ops_per_line == (ops_per_line - 1u) && voxels_in_inv_lod > ops_per_line && v < (voxels_in_inv_lod - 1u))
    //                    ss << "\n    ";
    //                else if (v % 8 == 7u)
    //                    ss << " ";
    //            }
    //            voxels_in_inv_lod *= 8u;
    //            ss << "\n";
    //        }
    //        ss << "    -----------------------------------------------------------------------\n";
    //        ss << "    ";
    //        for (int c = 0; c < 7; c++) {
    //            ss << op_char[c] << ": " << op_count[c] << "  ";
    //        }
    //        ss << " | sum: " << (i - brick_encoding[0]);
    //
    //        Logger(Info) << ss.str();
}

std::vector<glm::uvec4> CompressedSegmentationVolume::createBrickPosBuffer(uint32_t brick_size) {
    uint32_t total = brick_size * brick_size * brick_size;
    std::vector<glm::uvec4> v(total);
    for (int i = 0; i < v.size(); i++)
        v[i] = glm::uvec4(enumBrickPos(i), 0u);
    return v;
}

static const char *operation_names[] = {"PARENT", "NEIGHBORX", "NEIGHBORY", "NEIGHBORZ", "PALETTE_D", "PALETTE_ADV", "PALETTE_LAST", "__unused__",
                                        "sPARENT", "sNEIGHBORX", "sNEIGHBORY", "sNEIGHBORZ", "sPALETTE_D", "sPALETTE_ADV", "sPALETTE_LAST", "s__unused__"};

/** We "simulate a decompression" of this brick to gather statistics of its operations, palette, etc. */
void CompressedSegmentationVolume::getBrickStatistics(std::map<std::string, float> &statistics, uint32_t brick_idx,
                                                      glm::uvec3 valid_brick_size) const {
    throw std::runtime_error("method not yet adapted for split encodings");
    //
    //        uint32_t beginE = m_brick_starts[brick_idx];
    //        uint32_t paletteE = m_brick_starts[brick_idx + 1u];
    //
    //        // reset the statistics
    //        uint32_t lod_count = getLodCountPerBrick();
    //        statistics["most_frequent_id"] = static_cast<float>(m_encoding[paletteE]);                      // most frequent ID in brick (fist palette index)
    //        for(const auto& name : operation_names)                                              // operation frequencies
    //            statistics[name] = 0.f;
    //
    //        for(uint32_t i = 0; i < lod_count; i++) {
    //            statistics["operation_count_lod_" + std::to_string(i)] = 0.f;           // number of 4bit encodings in lod
    //            statistics["palette_lod_" + std::to_string(i) + "_size"] = 0.f;         // size of palette (ideally == unique_ids_lod_*)
    //            statistics["unique_ids_lod_" + std::to_string(i)] = 0.f;                // number of "new" IDs in this lod for this brick
    //        }
    //        statistics["operation_count"] = 0.f;                                        // total number of 4bit encoding entries
    //        statistics["palette_size"] = 0.f;                                           // total palette size
    //        statistics["unique_ids"] = 0.f;                                             // total number of unique ids in this brick (in the original volume)
    //        statistics["rle_4bit_reduction"] = 0.f;                                     // how many 4 bit operations were saved by using RLE
    //
    //        // refine up to the LOD that was requested
    //        uint32_t child_index;    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index
    //
    //        ReadState readState = {.idxE=m_encoding[beginE], .in_detail_lod=false};
    //        if(m_rANS_mode != NIBBLE_ENC) {
    //            readState.idxE = (beginE + readState.idxE / 8) * 4;
    //            m_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
    //        }
    //
    //        uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
    //        uint32_t lod_width = m_brick_size;
    //        uint32_t parent_value;
    //
    //        // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
    //        std::vector<uint32_t> output_brick(m_brick_size * m_brick_size * m_brick_size, INVALID);
    //        // we track unique labels in the brick
    //        std::unordered_set<uint32_t> unique_values_in_brick;
    //        size_t total_delta_back_reference = 0ul;
    //        size_t total_delta_back_reference_count = 0ul;
    //        for (int lod = 0; lod < lod_count; lod++) {
    //
    //            // check if we ran into the detail layer and change the readState accordingly
    //            if(m_rANS_mode == DOUBLE_TABLE_RANS && lod == lod_count-1) {
    //                readState.in_detail_lod = true;
    //
    //                if(m_separate_detail) {
    //                    beginE = m_detail_starts[brick_idx]; // beginE now refers to another buffer (detail) and has to be changed
    //                    readState.idxE = (beginE + 1) * 4;
    //                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_detail_encodings.data());
    //                }
    //                else {
    //                    // Read the lod start from the brick header to start reading at the right encoding buffer index.
    //                    // We have to start at a fully padded uint32, because we switch the rANS decoder.
    //                    readState.idxE = (beginE + m_encoding[beginE + lod] / 8) * 4;
    //                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
    //                }
    //            }
    //
    //            assert((isUsingRANS() || (beginE + readState.idxE/8 < paletteE)) && "read pointer runs over palette pointer");
    //            assert(index_step > 0 && "decoding with invalid index step");
    //
    //            for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
    //                // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
    //                if (glm::any(glm::greaterThanEqual(enumBrickPos(i), valid_brick_size)))
    //                    continue;
    //
    //                // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
    //                child_index = (i % (index_step * 8)) / index_step;
    //                if (lod > 0 && i % (index_step * 8) == 0) {
    //
    //                    // if this subtree is already filled (because in a previous LOD we had a STOP_BIT for this area), the last element of this block is set and we can skip it
    //                    if (output_brick[i + (index_step * 7)] != INVALID) {
    //                        i += (index_step * 7);
    //                        continue;
    //                    }
    //
    //                    parent_value = output_brick[i];
    //                    assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
    //                }
    //
    //                // get the next operation and apply it (either progress in the current RLE or read the next entry)
    //                uint32_t operation = readNextLodOperation(beginE, readState);
    //                statistics["operation_count_lod_" + std::to_string(lod)] += 1.f;
    //                statistics[operation_names[operation]] += 1.f;
    //
    //                uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits
    //                if (operation_lsb == PARENT)
    //                    output_brick[i] = parent_value;
    //                else if (operation_lsb == NEIGHBOR_X)
    //                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i), child_index, lod_width, m_brick_size, 0);
    //                else if (operation_lsb == NEIGHBOR_Y)
    //                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i), child_index, lod_width, m_brick_size, 1);
    //                else if (operation_lsb == NEIGHBOR_Z)
    //                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i), child_index, lod_width, m_brick_size, 2);
    //                else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
    //                    output_brick[i] = m_encoding[paletteE--];
    //                    statistics["palette_lod_" + std::to_string(lod) + "_size"] += 1.f;
    //                    if(!unique_values_in_brick.contains(output_brick[i])) {
    //                        unique_values_in_brick.insert(output_brick[i]);
    //                        statistics["unique_ids_lod_" + std::to_string(lod)] += 1.f;
    //                    }
    //                }
    //                else if (operation_lsb == PALETTE_LAST) {
    //                    output_brick[i] = m_encoding[paletteE + 1];
    //                    total_delta_back_reference++;
    //                    total_delta_back_reference_count++;
    //                }
    //                else if (operation_lsb == PALETTE_D) {
    //                    uint32_t palette_delta = readNextLodOperation(beginE, readState) + 2u;
    //                    total_delta_back_reference += palette_delta;
    //                    total_delta_back_reference_count++;
    //                    output_brick[i] = m_encoding[paletteE + palette_delta];
    //                }
    //                else
    //                    assert(false && "unrecognized compression operation");
    //
    //                // stop traversal: fill all other parts of the brick with this value
    //                if ((operation & STOP_BIT) > 0u) {
    //                    // fill the whole subtree with the parent value
    //                    for (uint32_t n = i; n < i + index_step; n++) {
    //                        output_brick[n] = output_brick[i];
    //                    }
    //                }
    //
    //                assert(output_brick[i] != INVALID && "Set output element brick to forbidden magic value INVALID!");
    //            }
    //
    //            // move to the next LOD block with half the block width and an eight of the index_step respectively
    //            index_step /= 8;
    //            lod_width /= 2;
    //        }
    //
    //        for(uint32_t i = 0; i < lod_count; i++) {
    //            statistics["operation_count"] += statistics["operation_count_lod_" + std::to_string(i)];
    //            statistics["palette_size"] += statistics["palette_lod_" + std::to_string(i) + "_size"];
    //        }
    //        statistics["unique_ids"] = static_cast<float>(unique_values_in_brick.size());
    //        if(unique_values_in_brick.contains(0u)) {
    //            if(unique_values_in_brick.size() == 1u)
    //                statistics["brick_visibility"] = 0u; // emtpy / invisible
    //            else
    //                statistics["brick_visibility"] = 1u; // mixed occupancy
    //        }
    //        else {
    //            statistics["brick_visibility"] = 2u; // fully occupied
    //        }
    //
}

std::vector<std::map<std::string, float>> CompressedSegmentationVolume::gatherBrickStatistics() const {
    const glm::uvec3 brickCount = getBrickCount();
    std::vector<std::map<std::string, float>> statistics(brickCount.x * brickCount.y * brickCount.z);

    glm::uvec3 brick_pos;
#pragma omp parallel for num_threads(m_cpu_threads) default(none) private(brick_pos) shared(brickCount, statistics)
    for (uint32_t z = 0; z < brickCount.z; z++) {
        unsigned int thread_id = omp_get_thread_num();
        brick_pos.z = z; // we need that for omp...
        for (brick_pos.y = 0; brick_pos.y < brickCount.y; brick_pos.y++) {
            for (brick_pos.x = 0; brick_pos.x < brickCount.x; brick_pos.x++) {
                size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
                const uint32_t *brick_encoding = getBrickEncoding(brick_idx);
                const uint32_t brick_encoding_length = getBrickEncodingLength(brick_idx);

                m_encoder->getBrickStatistics(statistics[brick_idx], brick_encoding, brick_encoding_length,
                                              glm::clamp(m_volume_dim - brick_pos * m_brick_size,
                                                         glm::uvec3(0u), glm::uvec3(m_brick_size)));

                // add some extra values to statistics
                statistics[brick_idx]["brick_x"] = static_cast<float>(brick_pos.x); // x coordinate of brick
                statistics[brick_idx]["brick_y"] = static_cast<float>(brick_pos.y); // y coordinate of brick
                statistics[brick_idx]["brick_z"] = static_cast<float>(brick_pos.z); // z coordinate of brick
                // total size is the encoding + one single uint for the brick starts array
                statistics[brick_idx]["total_byte_size"] = static_cast<float>(brick_encoding_length + 1u) * sizeof(uint32_t);
                statistics[brick_idx]["palette_length"] = static_cast<float>(brick_encoding[m_encoder->getPaletteSizeHeaderIndex()]);
            }
        }
    }

    return statistics;
}

void CompressedSegmentationVolume::exportSingleBrickOperationsHex(const std::string &path) const {
    const uint32_t brick_idx = getBrickIndexCount() / 2;

    std::ofstream fout(path, std::ios::out);
    if (!fout.is_open())
        throw std::runtime_error("Could not open file " + path + ".txt");

    const uint32_t *encoding = getBrickEncoding(brick_idx);
    if (m_encoding_mode == NIBBLE_ENC) {
        uint32_t start4bit = encoding[0];                                                              // first entry of header is the lod start in number of 4 bit entries
        uint32_t end4bit = (getBrickEncodingLength(brick_idx) - getBrickPaletteLength(brick_idx)) * 8; // (total brick size - palette size) * 8

        for (uint32_t i = start4bit; i < end4bit; i++) {
            unsigned char operation = read4Bit(encoding, 0, i);
            if (operation >= 16)
                throw std::runtime_error("4 bit operation must be < 16");

            char hex_code = (operation < 10) ? ('0' + operation) : ('A' + (operation - 10));
            fout << hex_code;
        }
    }
    fout.close();

    Logger(Info) << "exported csgv operations of center brick as hex codes to" << path;
}

void CompressedSegmentationVolume::exportAllBrickOperations(const std::string &path) const {
    if (m_encodings.empty() || m_separate_detail)
        throw std::runtime_error("Compress the volume without detail separation first before exporting brick operations!");

    // brick starts writes two uint32 numbers per brick:
    // [s] first operation of the brick in fout [d] index at which the detail LoD starts
    //
    // fout writes a back to back list of the operations of all bricks.

    std::ofstream fout(path + "_op.raw", std::ios::out | std::ios::binary);
    if (!fout.is_open())
        throw std::runtime_error("Could not open file " + path + "_op.raw");
    std::ofstream bs_out(path + "_op_starts.raw", std::ios::out | std::ios::binary);
    if (!bs_out.is_open())
        throw std::runtime_error("Could not open file " + path + "_starts.raw");

    // dummy file export just outputs ascending numbers to [*]_op.raw
    constexpr bool DUMMY_DATA_OUTPUT = false;

    const uint32_t brickCount = getBrickIndexCount();
    const uint32_t lod_count = getLodCountPerBrick();
    uint32_t top_pointer = 0;
    //        for (uint32_t brick_idx = 0; brick_idx < brickCount; brick_idx++) {
    for (uint32_t brick_idx = getBrickIndexCount() / 2; brick_idx < getBrickIndexCount() / 2 + 1; brick_idx++) {
        const uint32_t *encoding = getBrickEncoding(brick_idx);
        if (m_encoding_mode == NIBBLE_ENC) {
            uint32_t start4bit = encoding[0];                                                              // first entry of header is the lod start in number of 4 bit entries
            uint32_t end4bit = (getBrickEncodingLength(brick_idx) - getBrickPaletteLength(brick_idx)) * 8; // (total brick size - palette size) * 8

            if (static_cast<size_t>(top_pointer) + (end4bit - start4bit) >= UINT32_MAX) {
                Logger(Error) << "exceeding 32 bit index limit for operation export. Stopping export before brick " << brick_idx << " out of " << getBrickIndexCount();
                break;
            }

            // write the index at which this brick starts in the encoding array
            bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));

            // write at which index (0 indexed from first operation of the brick in .raw).
            // the detail level encoding starts that does not contain stop bits
            uint32_t base_lod_operation_count = encoding[getLodCountPerBrick() - 1] - start4bit;
            bs_out.write(reinterpret_cast<char *>(&base_lod_operation_count), sizeof(uint32_t));

            for (uint32_t i = start4bit; i < end4bit; i++) {
                unsigned char operation = read4Bit(encoding, 0, i);

                if (DUMMY_DATA_OUTPUT) {
                    // Dummy file export: ascending indices 0 1 2.. with max. value 7 in base and 15 in detail levels
                    operation = (i >= encoding[getLodCountPerBrick() - 1]) ? ((i - start4bit) % 8) : ((i - start4bit) % 16);
                }

                if (operation >= 16)
                    throw std::runtime_error("4 bit operation must be < 16");
                fout.write(reinterpret_cast<char *>(&operation), sizeof(unsigned char));
                top_pointer++;
            }
        } else {
            uint32_t start32bit = encoding[0] / 8u;                                                     // first entry of header is the lod start in number of 4 bit entries
            uint32_t end32bit = (getBrickEncodingLength(brick_idx) - getBrickPaletteLength(brick_idx)); // (total brick size - palette size) * 8

            if (static_cast<size_t>(top_pointer) + (end32bit - start32bit) >= UINT32_MAX) {
                Logger(Error) << "exceeding 32 bit index limit for operation export. Stopping export before brick " << brick_idx << " out of " << getBrickIndexCount();
                break;
            }

            bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));

            // write at which uint32 index (0 indexed from brick start) the detail level encoding starts that does not contain stop bits
            uint32_t base_lod_operation_count = encoding[getLodCountPerBrick() - 1] / 8u - start32bit;
            bs_out.write(reinterpret_cast<char *>(&base_lod_operation_count), sizeof(uint32_t));

            for (uint32_t i = start32bit; i < end32bit; i++) {
                uint32_t operations = encoding[i];
                fout.write(reinterpret_cast<char *>(&operations), sizeof(uint32_t));
                top_pointer++;
            }
        }
    }
    // write one dummy entry at the end to denote the end of the last brick with a detail start size of 0
    bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));
    top_pointer = 0u;
    bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));

    fout.close();
    bs_out.close();

    Logger(Info) << "exported " << (DUMMY_DATA_OUTPUT ? "DUMMY " : "")
                 << "csgv operations as " << ((m_encoding_mode == NIBBLE_ENC) ? " 4 bit codes " : " rANS stream")
                 << "to " << path << "_op.raw and [*]_op_starts.raw";

    if (m_encoding_mode == NIBBLE_ENC) {
        // IMPORT OF 4BIT OPERATION STREAM:
        std::ifstream raw_in(path + "_op.raw", std::ios::in | std::ios::binary);
        std::ifstream bs_in(path + "_op_starts.raw", std::ios::in | std::ios::binary);
        if (!raw_in.is_open() || !bs_in.is_open())
            throw std::runtime_error("Could not open file " + path + "_op[_starts].raw");

        // read bricks from the raw file, each brick consists of an operation stream between start_index and end_index
        uint32_t brick_start_index_in_raw = 0u;
        bs_in.read(reinterpret_cast<char *>(&brick_start_index_in_raw), sizeof(uint32_t));
        if (brick_start_index_in_raw != 0u)
            throw std::runtime_error("Invalid fist entry in starts file");

        uint32_t first_op_in_detail_level = 0;
        uint32_t brick_end_index_in_raw = 0;
        while (true) {
            // read index of first operation in finest level-of-detail
            bs_in.read(reinterpret_cast<char *>(&first_op_in_detail_level), sizeof(uint32_t));

            // read end index of brick
            bs_in.read(reinterpret_cast<char *>(&brick_end_index_in_raw), sizeof(uint32_t));
            if (bs_in.eof()) {
                bs_in.read(reinterpret_cast<char *>(&first_op_in_detail_level), sizeof(uint32_t));
                if (first_op_in_detail_level != 0u)
                    throw std::runtime_error("[*]_op_starts.raw file does not end with magic zero");
                break;
            }

            if (first_op_in_detail_level >= (brick_end_index_in_raw - brick_start_index_in_raw))
                throw std::runtime_error("[*]_op_starts.raw file does not end with magic zero");

            // read all operations of this brick, (brick_end_index_in_raw - brick_start_index_in_raw) in total
            unsigned char operation;
            for (uint32_t i = 0u; i < (brick_end_index_in_raw - brick_start_index_in_raw); i++) {
                raw_in.read(reinterpret_cast<char *>(&operation), sizeof(unsigned char));
                if (raw_in.eof())
                    throw std::runtime_error("Unexpected end of file!");

                if (i >= first_op_in_detail_level) {
                    // operations are in the value domain [0, 8)
                    if (operation >= 8)
                        throw std::runtime_error("invalid operation code in finest level-of-detail");
                } else {
                    // operations are in the value domain [0, 16) as they may contain a 1 in the 4th bit for stop codes
                    if (operation >= 16)
                        throw std::runtime_error("invalid operation code in base level-of-detail");
                }

                if (DUMMY_DATA_OUTPUT) {
                    // dummy file contains repeated ascending operation codes 0 1 2 3 4.. Sanity check:
                    if (i >= first_op_in_detail_level && (operation != (i % 8)) || i < first_op_in_detail_level && (operation != (i % 16))) {
                        throw std::runtime_error("Dummy file should contain unsigned ints in ascending order!");
                    }
                }

                // ... do something, create ab buffer of these brick's operations etc!
            }

            // next brick starts at current end index
            brick_start_index_in_raw = brick_end_index_in_raw;
        }
        raw_in.close();
        bs_in.close();
    }
}

void CompressedSegmentationVolume::exportBrickOperationsToCSV(const std::string &path, uint32_t brick_idx) const {
    //
    //        if(m_encodings.empty() || m_rANS_mode != NIBBLE_ENC || m_separate_detail)
    //            throw std::runtime_error("Compress the volume without rANS encoding and without detail separation first before exporting brick codes!");
    //        uint32_t lod_count = getLodCountPerBrick();
    //        uint32_t beginE = m_brick_starts[brick_idx];
    //        uint32_t start4bit = m_encoding[m_brick_starts[brick_idx]]; // first entry of header is the lod start in number of 4 bit entries
    //        uint32_t end4bit = (m_brick_starts[brick_idx+1] - m_brick_starts[brick_idx] - m_encoding[m_brick_starts[brick_idx] + 2u * lod_count]) * 8; // (total brick size - palette size) * 8
    //
    //        // print LoD start points
    //        std::stringstream ss_head("");
    //        for(int i = 0; i < lod_count; i++) {
    //            ss_head << "LoD" << i << ": " << (m_encoding[m_brick_starts[brick_idx] + i] - start4bit) << " ";
    //        }
    //        Logger(Info) << "exporting example brick with start indices " << ss_head.str();
    //
    //        std::ofstream fout(path, std::ios::out);
    //        assert(fout.is_open());
    //
    //        std::stringstream ss;
    //        for(uint32_t i = start4bit; i < end4bit; i++) {
    //            uint32_t operation = read4Bit(m_encoding, beginE, i);
    //            assert(operation < 16 && "4 bit operation must be < 16");
    //            ss << operation;
    //            if(i < end4bit - 1)
    //                ss << ",";
    //        }
    //
    //        fout << ss.str() << "\n";
    //        fout.close();
}

} // namespace volcanite
