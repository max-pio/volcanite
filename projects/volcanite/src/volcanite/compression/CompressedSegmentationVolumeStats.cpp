#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include <map>
#include <sstream>
#include <unordered_set>
#include <thread>

#include "volcanite/compression/bitpack.hpp"

namespace vvv {

    void CompressedSegmentationVolume::printBrickInfo(glm::uvec3 brick, loglevel log_level) const {
        if(m_encoding.empty())
            throw std::runtime_error("Segmentation volume is not yet compressed!");

        std::stringstream ss;
        uint32_t start = m_brick_starts[brick_pos2idx(brick, getBrickCount())];
        uint32_t p = start;
        ss << "Brick " << str(brick) << " " << getLodCountPerBrick() << "xLoD [Header @" << p << "] LoD Starts: ";
        if(isUsingSeparateDetail()) {
            for(int i = 0; i < getLodCountPerBrick() - 1; i++) {
                ss << std::to_string(m_encoding[p++]);
                if(i < getLodCountPerBrick() - 2)
                    ss << ",";
            }
            ss << " | LoD Palette Start: ";
            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
                ss << std::to_string(m_encoding[p++]);
                if(i < getLodCountPerBrick())
                    ss << ",";
            }
            ss << " (header size " << (p - start) << ") ";
            ss << " [Encoding @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_encoding[start + getLodCountPerBrick() - 2]); i++) {
                ss << m_encoding[p++] << ",";
            }
            start =  m_detail_starts[brick_pos2idx(brick, getBrickCount())];
            p = start;
            ss << ".. [Detail @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_detail_encoding[start]); i++) {
                ss << m_detail_encoding[p++] << ",";
            }
            ss << "..";
        }
        else {
            for(int i = 0; i < getLodCountPerBrick(); i++) {
                ss << std::to_string(m_encoding[p]);
                if(i < getLodCountPerBrick() - 1)
                    ss << ",";
                p++;
            }
            ss << " | LoD Palette Size: ";
            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
                ss << std::to_string(m_encoding[p]);
                if(i < getLodCountPerBrick())
                    ss << ",";
                p++;
            }
            ss << " (header size " << (p - start) << ") ";
            ss << " [Encoding @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_encoding[start + getLodCountPerBrick() - 2]); i++) {
                ss << m_encoding[p++] << ",";
            }
        }
        Logger(log_level) << ss.str();
    }

    void CompressedSegmentationVolume::decodeBrickWithDebugEncoding(uint32_t brick_idx, uint32_t* output_brick, uint32_t* output_encoding,
                                                     std::vector<glm::uvec4>* output_palette, glm::uvec3 valid_brick_size, int inv_lod) const {

        uint32_t beginE = m_brick_starts[brick_idx];
        // the palette starts at the end of the encoding block
        uint32_t paletteE = m_brick_starts[brick_idx + 1] - 1;

        // first: read the header (= LOD start positions)
        uint32_t lod_count = getLodCountPerBrick();
        std::vector<uint32_t> lod_starts(m_encoding.begin() + beginE, m_encoding.begin() + beginE + lod_count);

        // refine up to the LOD that was requested
        uint32_t child_index;    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

        ReadState readState = {.idxE=lod_starts[0]};
        if(m_rANS_mode != NO_RANS) {
            readState.idxE = (beginE + lod_starts[0] / 8) * 4;
            m_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
        }

        uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
        uint32_t lod_width = m_brick_size;
        uint32_t parent_value;

        // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
        for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
            output_brick[i] = INVALID;
            output_encoding[i] = INVALID;
        }
        if(output_palette) {
            output_palette->resize(inv_lod + 2, glm::uvec4(0u));
        }
        std::map<uint32_t, uint32_t> output_palette_duplicates;

        for (int lod = 0; lod <= inv_lod; lod++) {
            assert(lod <= lod_count);
            assert(index_step > 0);

            if(output_palette)
                output_palette->at(lod) = glm::uvec4(output_palette->size());

            // check if we ran into the detail layer and change the readState accordingly
            if(m_rANS_mode == DOUBLE_TABLE_RANS && lod == lod_count-1) {
                readState.in_detail_lod = true;
                if(m_separate_detail) {
                    beginE = m_detail_starts[brick_idx]; // beginE now refers to another buffer (detail) and has to be changed
                    readState.idxE = beginE * 4;
                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_detail_encoding.data());
                }
                else {
                    // Read the lod start from the brick header to start reading at the right encoding buffer index.
                    // We have to start at a fully padded uint32, because we switch the rANS decoder.
                    readState.idxE = (beginE + m_encoding[beginE + lod] / 8) * 4;
                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
                }
            }

            assert((isUsingRANS() || (beginE + readState.idxE/8 < paletteE)) && "read pointer runs over palette pointer");
            assert(index_step > 0 && "decoding with invalid index step");

            for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
                // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
                if (glm::any(glm::greaterThanEqual(enumBrickPos(i, m_brick_size), valid_brick_size)))
                    continue;

                // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
                child_index = (i % (index_step * 8)) / index_step;
                if (lod > 0 && i % (index_step * 8) == 0) {

                    // if this subtree is already filled (because in a previous LOD we had a PARENT_STOP for this area), the last element of this block is set and we can skip it
                    if (output_brick[i + (index_step * 7)] != INVALID) {
                        // we have to remove the parent's encoding flag at this position
                        output_encoding[i] = INVALID;
                        i += (index_step * 7);
                        continue;
                    }

                    parent_value = output_brick[i];
                    assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
                }

                // get the next operation and apply it
                uint32_t operation = readNextLodOperation(beginE, readState);
                output_encoding[i] = operation;

                uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits with 0111
                if (operation_lsb == PARENT)
                    output_brick[i] = parent_value;
                else if (operation_lsb == NEIGHBOR_X)
                    output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 0);
                else if (operation_lsb == NEIGHBOR_Y)
                    output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 1);
                else if (operation_lsb == NEIGHBOR_Z)
                    output_brick[i] = valueOfNeighbor(output_brick, enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 2);
                else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
                    output_brick[i] = m_encoding[paletteE--];
                    if(output_palette) {
                        auto value = output_brick[i];
                        if(!output_palette_duplicates.contains(value)) {
                            output_palette_duplicates[value] = 0u;
                        }
                        output_palette->push_back(glm::uvec4{value, lod, i, output_palette_duplicates[value]});
                        output_palette_duplicates[value]++;
                    }
                }
                else if (operation_lsb == PALETTE_LAST) {
                    output_brick[i] = m_encoding[paletteE + 1];
                }
                else if (operation_lsb == PALETTE_D) {
                    uint32_t palette_delta = readNextLodOperation(beginE, readState) + 2u;
                    output_brick[i] = m_encoding[paletteE + palette_delta];
                }
                else
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
        if(output_palette)
            output_palette->at(inv_lod + 1) = glm::uvec4(output_palette->size());
    }

    std::vector<glm::uvec4> CompressedSegmentationVolume::createBrickPosBuffer(uint32_t brick_size) {
        uint32_t total = brick_size * brick_size * brick_size;
        std::vector<glm::uvec4> v(total);
        for(int i = 0; i < v.size(); i++)
        v[i] = glm::uvec4(enumBrickPos(i, brick_size), 0u);
        return v;
    }

    static const char* operation_names[] = {"PARENT", "NEIGHBORX", "NEIGHBORY", "NEIGHBORZ", "PALETTE_D", "PALETTE_ADV", "PALETTE_LAST", "__unused__",
                                            "sPARENT", "sNEIGHBORX", "sNEIGHBORY", "sNEIGHBORZ", "sPALETTE_D", "sPALETTE_ADV", "sPALETTE_LAST", "s__unused__"};

    /** We "simulate a decompression" of this brick to gather statistics of its operations, palette, etc. */
    void CompressedSegmentationVolume::getBrickStatistics(std::map<std::string, float> &statistics, uint32_t brick_idx, glm::uvec3 valid_brick_size) const {
        uint32_t beginE = m_brick_starts[brick_idx];
        uint32_t paletteE = m_brick_starts[brick_idx + 1u];

        // reset the statistics
        uint32_t lod_count = getLodCountPerBrick();
        statistics["most_frequent_id"] = static_cast<float>(m_encoding[paletteE]);                      // most frequent ID in brick (fist palette index)
        for(const auto& name : operation_names)                                              // operation frequencies
            statistics[name] = 0.f;

        for(uint32_t i = 0; i < lod_count; i++) {
            statistics["operation_count_lod_" + std::to_string(i)] = 0.f;           // number of 4bit encodings in lod
            statistics["palette_lod_" + std::to_string(i) + "_size"] = 0.f;         // size of palette (ideally == unique_ids_lod_*)
            statistics["unique_ids_lod_" + std::to_string(i)] = 0.f;                // number of "new" IDs in this lod for this brick
        }
        statistics["operation_count"] = 0.f;                                        // total number of 4bit encoding entries
        statistics["palette_size"] = 0.f;                                           // total palette size
        statistics["unique_ids"] = 0.f;                                             // total number of unique ids in this brick (in the original volume)
        statistics["rle_4bit_reduction"] = 0.f;                                     // how many 4 bit operations were saved by using RLE

        // refine up to the LOD that was requested
        uint32_t child_index;    // index of all children with the same coarser parent element, in 0 - 7, used for parent_value and neighbor-lookup index

        ReadState readState = {.idxE=m_encoding[beginE], .in_detail_lod=false};
        if(m_rANS_mode != NO_RANS) {
            readState.idxE = (beginE + readState.idxE / 8) * 4;
            m_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
        }

        uint32_t index_step = m_brick_size * m_brick_size * m_brick_size;
        uint32_t lod_width = m_brick_size;
        uint32_t parent_value;

        // first, set the whole brick to INVALID, so we know later which elements and LOD blocks were already processed
        std::vector<uint32_t> output_brick(m_brick_size * m_brick_size * m_brick_size, INVALID);
        // we track unique labels in the brick
        std::unordered_set<uint32_t> unique_values_in_brick;
        size_t total_delta_back_reference = 0ul;
        size_t total_delta_back_reference_count = 0ul;
        for (int lod = 0; lod < lod_count; lod++) {

            // check if we ran into the detail layer and change the readState accordingly
            if(m_rANS_mode == DOUBLE_TABLE_RANS && lod == lod_count-1) {
                readState.in_detail_lod = true;

                if(m_separate_detail) {
                    beginE = m_detail_starts[brick_idx]; // beginE now refers to another buffer (detail) and has to be changed
                    readState.idxE = (beginE + 1) * 4;
                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_detail_encoding.data());
                }
                else {
                    // Read the lod start from the brick header to start reading at the right encoding buffer index.
                    // We have to start at a fully padded uint32, because we switch the rANS decoder.
                    readState.idxE = (beginE + m_encoding[beginE + lod] / 8) * 4;
                    m_detail_rans.itr_initDecoding(readState.rans_state, readState.idxE, m_encoding.data());
                }
            }

            assert((isUsingRANS() || (beginE + readState.idxE/8 < paletteE)) && "read pointer runs over palette pointer");
            assert(index_step > 0 && "decoding with invalid index step");

            for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i += index_step) {
                // if a grid node is completely outside the volume (i.e. it's first element is not within the volume) we skip it as it won't have any entries in the encoding
                if (glm::any(glm::greaterThanEqual(enumBrickPos(i, m_brick_size), valid_brick_size)))
                    continue;

                // every 8th element (we span 2*2*2=8 elements of the coarse LOD above), we fetch the new parent
                child_index = (i % (index_step * 8)) / index_step;
                if (lod > 0 && i % (index_step * 8) == 0) {

                    // if this subtree is already filled (because in a previous LOD we had a PARENT_STOP for this area), the last element of this block is set and we can skip it
                    if (output_brick[i + (index_step * 7)] != INVALID) {
                        i += (index_step * 7);
                        continue;
                    }

                    parent_value = output_brick[i];
                    assert(parent_value != INVALID && "parent element in brick was not set in previous LOD!");
                }

                // get the next operation and apply it (either progress in the current RLE or read the next entry)
                uint32_t operation = readNextLodOperation(beginE, readState);
                statistics["operation_count_lod_" + std::to_string(lod)] += 1.f;
                statistics[operation_names[operation]] += 1.f;

                uint32_t operation_lsb = operation & 7u; // extract least significant 3 bits
                if (operation_lsb == PARENT)
                    output_brick[i] = parent_value;
                else if (operation_lsb == NEIGHBOR_X)
                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 0);
                else if (operation_lsb == NEIGHBOR_Y)
                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 1);
                else if (operation_lsb == NEIGHBOR_Z)
                    output_brick[i] = valueOfNeighbor(output_brick.data(), enumBrickPos(i, m_brick_size), child_index, lod_width, m_brick_size, 2);
                else if (operation_lsb == PALETTE_ADV) { // read palette entry and advance palette pointer to the next entry
                    output_brick[i] = m_encoding[paletteE--];
                    statistics["palette_lod_" + std::to_string(lod) + "_size"] += 1.f;
                    if(!unique_values_in_brick.contains(output_brick[i])) {
                        unique_values_in_brick.insert(output_brick[i]);
                        statistics["unique_ids_lod_" + std::to_string(lod)] += 1.f;
                    }
                }
                else if (operation_lsb == PALETTE_LAST) {
                    output_brick[i] = m_encoding[paletteE + 1];
                    total_delta_back_reference++;
                    total_delta_back_reference_count++;
                }
                else if (operation_lsb == PALETTE_D) {
                    uint32_t palette_delta = readNextLodOperation(beginE, readState) + 2u;
                    total_delta_back_reference += palette_delta;
                    total_delta_back_reference_count++;
                    output_brick[i] = m_encoding[paletteE + palette_delta];
                }
                else
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

        for(uint32_t i = 0; i < lod_count; i++) {
            statistics["operation_count"] += statistics["operation_count_lod_" + std::to_string(i)];
            statistics["palette_size"] += statistics["palette_lod_" + std::to_string(i) + "_size"];
        }
        statistics["unique_ids"] = static_cast<float>(unique_values_in_brick.size());
        if(unique_values_in_brick.contains(0u)) {
            if(unique_values_in_brick.size() == 1u)
                statistics["brick_visibility"] = 0u; // emtpy / invisible
            else
                statistics["brick_visibility"] = 1u; // mixed occupancy
        }
        else {
            statistics["brick_visibility"] = 2u; // fully occupied
        }

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
                    // decode brick
                    getBrickStatistics(statistics[brick_idx], brick_idx, glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)));
                    // add some extra values to statistics
                    statistics[brick_idx]["brick_x"] = static_cast<float>(brick_pos.x); // x coordinate of brick
                    statistics[brick_idx]["brick_y"] = static_cast<float>(brick_pos.y); // y coordinate of brick
                    statistics[brick_idx]["brick_z"] = static_cast<float>(brick_pos.z); // z coordinate of brick
                    statistics[brick_idx]["total_size"] = static_cast<float>(m_brick_starts[brick_idx + 1] - m_brick_starts[brick_idx] + 1u); // total size of brick in number of uint32_t (+1 uint for the brick_starts buffer)
                }
            }
        }

        return statistics;
    }

    void CompressedSegmentationVolume::exportAllBrickOperations(const std::string& path) const {
        if(m_encoding.empty() || m_separate_detail)
            throw std::runtime_error("Compress the volume without detail separation first before exporting brick operations!");

        // brick starts writes two uint32 numbers per brick:
        // [s] first operation of the brick in fout [d] index at which the detail LoD starts
        //
        // fout writes a back to back list of the operations of all bricks.


        std::ofstream fout(path + "_op.raw", std::ios::out | std::ios::binary);
        if(!fout.is_open())
            throw std::runtime_error("Could not open file " + path + ".raw");
        std::ofstream bs_out(path + "_op_starts.raw", std::ios::out | std::ios::binary);
        if(!bs_out.is_open())
            throw std::runtime_error("Could not open file " + path + "_starts.raw");

        const glm::uvec3 brickCount = getBrickCount();
        uint32_t top_pointer = 0;
        for (uint32_t brick_idx = 0; brick_idx < brickCount.x * brickCount.y * brickCount.z; brick_idx++) {
            uint32_t lod_count = getLodCountPerBrick();
            uint32_t beginE = m_brick_starts[brick_idx];
            if(m_rANS_mode == NO_RANS) {
                uint32_t start4bit = m_encoding[m_brick_starts[brick_idx]]; // first entry of header is the lod start in number of 4 bit entries
                uint32_t end4bit = (m_brick_starts[brick_idx + 1] - m_brick_starts[brick_idx] - m_encoding[m_brick_starts[brick_idx] + 2u * lod_count]) * 8; // (total brick size - palette size) * 8

                // write the index at which this brick starts in the encoding array
                bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));

                // write at which index (0 indexed from brick start) the detail level encoding starts that does not contain stop bits
                uint32_t base_lod_operation_count = m_encoding[m_brick_starts[brick_idx] + getLodCountPerBrick() - 1] - start4bit;
                bs_out.write(reinterpret_cast<char * >(&base_lod_operation_count), sizeof(uint32_t));

                for (uint32_t i = start4bit; i < end4bit; i++) {
                    uint32_t operation = read4Bit(m_encoding, beginE, i);
                    if (operation >= 16)
                        throw std::runtime_error("4 bit operation must be < 16");
                    fout.write(reinterpret_cast<char *>(&operation), sizeof(uint32_t));
                    top_pointer++;
                }
            } else {
                uint32_t start32bit = m_encoding[m_brick_starts[brick_idx]] / 8u; // first entry of header is the lod start in number of 4 bit entries
                uint32_t end32bit = (m_brick_starts[brick_idx + 1] - m_brick_starts[brick_idx] - m_encoding[m_brick_starts[brick_idx] + 2u * lod_count]); // (total brick size - palette size) * 8

                bs_out.write(reinterpret_cast<char *>(&top_pointer), sizeof(uint32_t));

                // write at which uint32 index (0 indexed from brick start) the detail level encoding starts that does not contain stop bits
                uint32_t base_lod_operation_count = m_encoding[m_brick_starts[brick_idx] + getLodCountPerBrick() - 1] / 8u - start32bit;
                bs_out.write(reinterpret_cast<char * >(&base_lod_operation_count), sizeof(uint32_t));

                for (uint32_t i = start32bit; i < end32bit; i++) {
                    uint32_t operations = m_encoding[i];
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

        Logger(INFO) << "exported csgv operations to " << path << "_op.raw";
        /*
        // IMPORT:
        std::ifstream raw_in(path + ".raw", std::ios::in | std::ios::binary);
        std::ifstream bs_in(path + "_starts.raw", std::ios::in | std::ios::binary);
        if(!raw_in.is_open() || !bs_in.is_open())
            throw std::runtime_error("Could not open file " + path + "*.raw");
        // read bricks from the raw file, each brick consists of an operation stream between start_index and end_index
        uint32_t brick_start_index_in_raw = 0u;
        bs_in.read(reinterpret_cast<char *>(&brick_start_index_in_raw), sizeof(uint32_t));
        if(brick_start_index_in_raw != 0u)
            throw std::runtime_error("Invalid fist entry in starts file");
        uint32_t brick_end_index_in_raw = 0;
        while(true) {
            // read end index of brick
            bs_in.read(reinterpret_cast<char *>(&brick_end_index_in_raw), sizeof(uint32_t));
            if(bs_in.eof())
                break;

            // read all operations of brick
            uint32_t operation;
            for(uint32_t i = 0u; i < (brick_end_index_in_raw - brick_start_index_in_raw); i++) {
                raw_in.read(reinterpret_cast<char *>(&operation), sizeof(uint32_t));
                if(raw_in.eof())
                    throw std::runtime_error("Unexpected end of file!");

                // if(operation != i) // dummy file sanity check
                //     throw std::runtime_error("Dummy file should contain unsigned ints in ascending order!");

                // ... do something, create ab buffer of these brick's operations etc!
            }

            // next brick starts at current end index
            brick_start_index_in_raw = brick_end_index_in_raw;
        }
        raw_in.close();
        bs_in.close();
         */
    }

    void CompressedSegmentationVolume::exportBrickOperationsToCSV(const std::string& path, uint32_t brick_idx) const {
        if(m_encoding.empty() || m_rANS_mode != NO_RANS || m_separate_detail)
            throw std::runtime_error("Compress the volume without rANS encoding and without detail separation first before exporting brick codes!");
        uint32_t lod_count = getLodCountPerBrick();
        uint32_t beginE = m_brick_starts[brick_idx];
        uint32_t start4bit = m_encoding[m_brick_starts[brick_idx]]; // first entry of header is the lod start in number of 4 bit entries
        uint32_t end4bit = (m_brick_starts[brick_idx+1] - m_brick_starts[brick_idx] - m_encoding[m_brick_starts[brick_idx] + 2u * lod_count]) * 8; // (total brick size - palette size) * 8

        // print LoD start points
        std::stringstream ss_head("");
        for(int i = 0; i < lod_count; i++) {
            ss_head << "LoD" << i << ": " << (m_encoding[m_brick_starts[brick_idx] + i] - start4bit) << " ";
        }
        Logger(INFO) << "exporting example brick with start indices " << ss_head.str();

        std::ofstream fout(path, std::ios::out);
        assert(fout.is_open());

        std::stringstream ss;
        for(uint32_t i = start4bit; i < end4bit; i++) {
            uint32_t operation = read4Bit(m_encoding, beginE, i);
            assert(operation < 16 && "4 bit operation must be < 16");
            ss << operation;
            if(i < end4bit - 1)
                ss << ",";
        }

        fout << ss.str() << "\n";
        fout.close();
    }

} // namespace vvv
