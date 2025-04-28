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

#pragma once

#include <memory>

#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/compression/memory_mapping.hpp"

using namespace vvv;

namespace volcanite {

class CSGVChunkMerger {
  private:
    // full volume properties
    glm::ivec3 chunk_count = {0, 0, 0};
    uint32_t total_chunk_count = 0u;
    glm::uvec3 brick_count = {0u, 0u, 0u};
    size_t total_brick_count = 0ul;
    uint32_t max_brick_palette_count = 0u;

    // all previously compressed chunks
    CompressedSegmentationVolume *chunks = nullptr;
    // (inner) chunk properties
    glm::uvec3 chunk_dimension = {0u, 0u, 0u};
    glm::uvec3 bricks_in_chunk = {0u, 0u, 0u};
    std::vector<uint32_t> reference_frequency_table = {};
    std::vector<uint32_t> reference_detail_frequency_table = {};

    /** Obtains the memory region in which the encoding for the given output brick is located.
     * @param output_brick brick position in the merged output volume
     * @param encoding pointer to uint* to which the start address of the brick encoding is written
     * @param encoding_length pointer to uint to which the length of the brick encoding in number of uints is written.
     */
    void getEncodingForOutputBrick(glm::uvec3 output_brick, const uint32_t **encoding, uint32_t *encoding_length) {
        // determine input chunk index and local brick position within input chunk
        glm::uvec3 chunk_pos = output_brick / bricks_in_chunk;
        glm::uvec3 brick_in_chunk = output_brick - chunk_pos * bricks_in_chunk;
        uint32_t chunk_idx = sfc::Cartesian::p2i(chunk_pos, chunk_count);
        uint32_t brick_idx_in_chunk = brick_pos2idx(brick_in_chunk, chunks[chunk_idx].getBrickCount());
        // return encoding start address and length in uint entries
        *encoding = chunks[chunk_idx].getBrickEncoding(brick_idx_in_chunk);
        *encoding_length = chunks[chunk_idx].getBrickEncodingLength(brick_idx_in_chunk);
    }

  public:
    CSGVChunkMerger() = default;

    ~CSGVChunkMerger() {
        delete[] chunks;
    }

    /** Merges all CompressedSegmentationVolume files from the individually compressed chunks given by a formatted path and maximum file index into one CompressedSegmentationVolume.
     * The formatted_path can contain zero up to three {} placeholders that will be replaced with the respective indices from 0 up to max_file_index.x|y|z, e.g:
     * "test_{}_{}_{}" will be replaced up to "test_1_2_3" with max_file_index=(1,2,3)".
     * All input chunk csgv files must use the same brick size, rANS mode, and rANS frequency tables and must not use detail separation.
     * Any input chunk csgv file with chunk indices smaller than max_input_csgv_index must have a volume dimension that is evenly dividable by the brick size.
     * @param output_csgv_path output *.csgv file to which the merged tiles are exported
     * @param input_csgv_template_path formatted path of input chunk *.csgv files
     * @param max_input_csgv_index inclusive last x y z tile indices to merge
     */
    std::shared_ptr<CompressedSegmentationVolume> mergeCompressedSegmentationVolumeChunksFromFiles(const std::string &output_csgv_path,
                                                                                                   const std::string &input_csgv_template_path,
                                                                                                   glm::ivec3 max_input_csgv_index) {
        // TODO: make target_uints_per_split_encoding a parameter for merging or obtain it from the first input chunk
        // target a size of ~2GB per split encoding vector
        static constexpr uint32_t target_uints_per_split_encoding = 536870912u;

        Logger(Info, true) << "Merging Compressed Segmentation Volume chunk files 0%";

        // our final filename
        if (std::filesystem::exists(output_csgv_path)) {
            Logger(Warn) << "File " << output_csgv_path << " already exists! Will be overwritten.";
        }

        // 1. load all chunk CSGV files into memory
        chunk_count = max_input_csgv_index + glm::ivec3(1);
        total_chunk_count = chunk_count.x * chunk_count.y * chunk_count.z;
        Logger(Info, true) << "Merging Compressed Segmentation Volume chunk files 0% (import " << total_chunk_count << " chunk csgv files)";
        chunks = new CompressedSegmentationVolume[total_chunk_count];
        for (uint32_t c = 0; c < total_chunk_count; c++) {
            glm::ivec3 chunk_index = sfc::Cartesian::i2p(c, chunk_count);
            chunks[c].importFromFile(formatChunkPath(input_csgv_template_path, chunk_index.x, chunk_index.y, chunk_index.z), false, false);

            // double check here as verifying the compression is cheap
            if (!chunks[c].verifyCompression()) {
                Logger(Error) << "Verification error when importing compressed chunk "
                              << formatChunkPath(input_csgv_template_path, chunk_index.x, chunk_index.y, chunk_index.z)
                              << " during merging.";
                return nullptr;
            }

            // keep track of maximum pallette entry count over all chunks
            max_brick_palette_count = glm::max(max_brick_palette_count, chunks[c].getMaxBrickPaletteCount());

            if (chunks[c].isUsingSeparateDetail()) {
                Logger(Error) << "Detail separation can only be applied AFTER merging Compressed Segmentation Volumes. Import CSGV chunks must not use detail separation.";
                return nullptr;
            }
            if (c == 0) {
                // store parameters of chunks
                chunk_dimension = chunks[0].getVolumeDim();
                chunks[0].m_encoding_mode = chunks[0].getEncodingMode();
                if ((chunk_count.x > 1 && chunk_dimension.x % chunks[0].m_brick_size != 0) || (chunk_count.y > 1 && chunk_dimension.y % chunks[0].m_brick_size != 0) || (chunk_count.z > 1 && chunk_dimension.z % chunks[0].m_brick_size != 0)) {
                    Logger(Error) << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunk dimension must be multiple of brick size.";
                    return nullptr;
                }
                bricks_in_chunk = (chunk_dimension + chunks[0].m_brick_size - glm::uvec3(1u)) / chunks[0].m_brick_size;

                if (chunks[0].m_encoding_mode == SINGLE_TABLE_RANS_ENC || chunks[0].m_encoding_mode == DOUBLE_TABLE_RANS_ENC)
                    reference_frequency_table = chunks[0].getCurrentFrequencyTable();
                if (chunks[0].m_encoding_mode == DOUBLE_TABLE_RANS_ENC)
                    reference_detail_frequency_table = chunks[0].getCurrentDetailFrequencyTable();
            } else {
                // check if chunk CSGV use the same compression parameters
                if (chunks[0].m_encoding_mode != chunks[c].getEncodingMode()) {
                    Logger(Error)
                        << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunks must use same encoding mode.";
                    return nullptr;
                }
                if ((!reference_frequency_table.empty() && reference_frequency_table != chunks[c].getCurrentFrequencyTable()) ||
                    (!reference_detail_frequency_table.empty() &&
                     reference_detail_frequency_table != chunks[c].getCurrentDetailFrequencyTable())) {
                    Logger(Error)
                        << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunks must use same rANS frequency tables.";
                    return nullptr;
                }
                if ((chunk_index.x < max_input_csgv_index.x && chunk_index.y < max_input_csgv_index.y && chunk_index.z < max_input_csgv_index.z) && (chunk_dimension.x != chunks[c].getVolumeDim().x || chunk_dimension.y != chunks[c].getVolumeDim().y || chunk_dimension.z != chunks[c].getVolumeDim().z)) {
                    Logger(Error)
                        << "Merging Compressed Segmentation Volume chunk files failed. Inner CSGV chunks must have the same volume dimensions.";
                    return nullptr;
                }
                // ToDo: check if volume dimensions of outer CSGV chunks fit
            }
        }
        glm::uvec3 complete_volume_dim = chunk_dimension * glm::uvec3(max_input_csgv_index) + chunks[sfc::Cartesian::p2i(max_input_csgv_index, chunk_count)].getVolumeDim();
        brick_count = (glm::uvec3(chunk_count - glm::ivec3(1)) * chunk_dimension + chunks[sfc::Cartesian::p2i(max_input_csgv_index, chunk_count)].getVolumeDim() - glm::uvec3(1)) / chunks[0].m_brick_size + 1u;
        total_brick_count = brick_count.x * brick_count.y * brick_count.z;
        if (total_brick_count > (1ull << 32) - 1ull) {
            Logger(Error)
                << "Merging Compressed Segmentation Volume chunk files failed. Brick count exceeds 32 bit range. Use a larger brick size.";
            return nullptr;
        }
        Logger(Info, true) << "Merging Compressed Segmentation Volume chunk files 0% (chunk import for " << str(complete_volume_dim) << " volume complete)";

        // 2. start by creating two tmp files where we construct the combined brickstarts and encoding buffers
        std::string brickstarts_path = output_csgv_path.substr(0, output_csgv_path.length() - 5) + "_brickstarts.tmp";
        if (std::filesystem::exists(brickstarts_path))
            Logger(Warn) << "Overwriting existing file " << brickstarts_path;
        std::ofstream brickstarts_file(brickstarts_path, std::ios_base::out | std::ios::binary);
        if (!brickstarts_file.is_open()) {
            Logger(Error) << "Unable to open file " << brickstarts_path << ". Skipping.";
            return nullptr;
        }
        std::string encoding_path = output_csgv_path.substr(0, output_csgv_path.length() - 5) + "_encoding.tmp";
        if (std::filesystem::exists(encoding_path))
            Logger(Warn) << "Overwriting existing file " << encoding_path;
        std::ofstream encoding_file(encoding_path, std::ios_base::out | std::ios::binary);
        if (!encoding_file.is_open()) {
            Logger(Error) << "Unable to open file " << encoding_path << ". Skipping.";
            brickstarts_file.close();
            return nullptr;
        }

        // 3. iterate over all output brick indices
        // a) get encoding memory area of brick from its corresponding input chunk csgv
        // b) determine brick_idx_to_enc_vector / start new split encoding arrays in output file
        // c) write brick encoding to encoding tmp file
        // d) write brick start within current output encoding array to brickstarts tmp file

        // split encoding vector management
        uint32_t brick_idx_to_enc_vector = ~0u;
        size_t split_encoding_count = 1u;
        size_t encoding_size = 0ull;

        // temporary encoding_file layout:
        // (split encoding count)x:
        //      1x uint32: encoding size
        //      (encoding size)x uint32: encoding entries

        // location in file to put uint32 size of the following split encoding once its export is finished
        long encoding_size_file_pos = encoding_file.tellp();
        // write a dummy entry for encoding size that will be overwritten once the first split encoding is written out
        encoding_file.write(reinterpret_cast<const char *>(&encoding_size), sizeof(size_t));

        for (uint32_t brick_idx = 0u; brick_idx < total_brick_count; brick_idx++) {

            // get encoding and encoding length of next output brick
            glm::uvec3 output_brick = brick_idx2pos(brick_idx, brick_count);
            const uint32_t *brick_encoding;
            uint32_t brick_encoding_size;
            getEncodingForOutputBrick(output_brick, &brick_encoding, &brick_encoding_size);

            // Write the current "brick start" before the possible splitting of encodings as it is the "previous brick end"
            uint32_t encoding_size_uint32_t = static_cast<uint32_t>(encoding_size);
            brickstarts_file.write(reinterpret_cast<const char *>(&encoding_size_uint32_t), sizeof(uint32_t));

            // Encoding splitting similar to CompressedSegmentationVolume::compress(..):
            // Check if the initial split must happen here (when the uint32_t element count exceeds target_uints_per_split_encoding)
            // We can not reduce brick_idx_to_enc_vector further if it was already used for splitting encoding vectors.
            // Otherwise, the old split may become invalid.
            if (split_encoding_count == 1u && encoding_size + brick_encoding_size > target_uints_per_split_encoding) {
                if (brick_idx == 0u) {
                    Logger(Warn) << "Requested split encoding size is too small. Using minimal size.";
                } else {
                    brick_idx_to_enc_vector = brick_idx;
                }
            }
            // Check if we have to start a new split encoding "vector" before writing the next brick's encoding.
            if (brick_idx / brick_idx_to_enc_vector >= split_encoding_count) {
                if (encoding_size != encoding_size_uint32_t) {
                    Logger(Error) << "Split encoding size overflow for array " << (split_encoding_count - 1)
                                  << ", uint size " << encoding_size;
                    return nullptr;
                } else if (encoding_size > target_uints_per_split_encoding) {
                    Logger(Debug) << "Brick index to encoding array mapping is underestimating sizes: "
                                  << "Split array " << (split_encoding_count - 1) << " with "
                                  << (static_cast<size_t>(encoding_size) * sizeof(uint32_t)) << " bytes.";
                }

                // write size of now finished previous split encoding to the previously reserved encoding size location
                long end_of_file = encoding_file.tellp();
                encoding_file.seekp(encoding_size_file_pos);
                encoding_file.write(reinterpret_cast<const char *>(&encoding_size), sizeof(size_t));

                // remember location to store size of next finished and write a temporary placeholder value
                encoding_size_file_pos = end_of_file;
                encoding_file.seekp(end_of_file);
                encoding_file.write(reinterpret_cast<const char *>(&encoding_size), sizeof(size_t));

                split_encoding_count++;
                encoding_size = 0ull;
            }

            // write current brick's encoding
            encoding_file.write(reinterpret_cast<const char *>(brick_encoding), static_cast<std::streamsize>(brick_encoding_size) * sizeof(uint32_t));
            encoding_size += brick_encoding_size;

            if ((static_cast<long>(brick_idx) * 100l / total_brick_count) * total_brick_count == static_cast<long>(brick_idx) * 100l) {
                Logger(Info, true) << "Merging Compressed Segmentation Volume chunk files " << std::fixed
                                   << std::setprecision(0)
                                   << 95.f * static_cast<float>(brick_idx) / static_cast<float>(total_brick_count)
                                   << "% (writing brick encodings)";
            }
        }

        // finish the last split encoding vector
        {
            // final dummy brick_starts entry to denote the length of the last brick encoding
            uint32_t encoding_size_uint32_t = static_cast<uint32_t>(encoding_size);
            if (encoding_size_uint32_t != encoding_size) {
                Logger(Error) << "Split encoding size overflow for array " << (split_encoding_count - 1) << ", size " << encoding_size;
                return nullptr;
            }
            brickstarts_file.write(reinterpret_cast<const char *>(&encoding_size_uint32_t), sizeof(uint32_t));
            // write the size of the current split encoding
            encoding_file.seekp(encoding_size_file_pos);
            encoding_file.write(reinterpret_cast<const char *>(&encoding_size), sizeof(size_t));
        }

        brickstarts_file.close();
        encoding_file.close();

        // 4. free memory of all chunks but the first (which is used to write out general header information)
        chunks[0].m_encodings.clear();
        chunks[0].m_brick_starts.clear();
        chunks[0].m_detail_encodings.clear();
        chunks[0].m_detail_starts.clear();
        for (uint32_t c = 1; c < total_chunk_count; c++) {
            chunks[c].clear();
        }
        // wait for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // 5. append tmp files together to form one valid csgv file
        {
            Logger(Info, true) << "Merging Compressed Segmentation Volume chunk files 95% (creating single file with complete volume)";
            if (std::filesystem::exists(output_csgv_path)) {
                std::filesystem::remove(output_csgv_path);
            }

            // open output and input file streams
            std::ofstream file(output_csgv_path, std::ios_base::out | std::ios::binary);
            if (!file.is_open()) {
                Logger(Error) << "Unable to open file " << output_csgv_path << " for writing. Skipping.";
                return nullptr;
            }

            // similar to CompressedSegmentationVolume::exportToFile(..)
            // write header: 8 chars CMPSGVOL + 4 chars version number
            const char *magic_header = "CMPSGVOL";
            const char *version = "0016";
            file.write(magic_header, 8);
            file.write(version, 4);

            // write general info
            file.write(reinterpret_cast<const char *>(&chunks[0].m_brick_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char *>(&complete_volume_dim), sizeof(glm::uvec3));
            file.write(reinterpret_cast<const char *>(&chunks[0].m_encoding_mode), sizeof(EncodingMode)); // since 0011
            file.write(reinterpret_cast<const char *>(&chunks[0].m_random_access), sizeof(bool));         // since 015
            file.write(reinterpret_cast<const char *>(&max_brick_palette_count), sizeof(uint32_t));
            file.write(reinterpret_cast<const char *>(&chunks[0].m_op_mask), sizeof(uint32_t)); // since 015
            chunks[0].m_encoder->exportToFile(file);                                            // since 015

            file.write(reinterpret_cast<const char *>(&brick_idx_to_enc_vector), sizeof(uint32_t)); // since 0013

            // free all remaining memory of CSGV chunks
            delete[] chunks;
            chunks = nullptr;
            // wait for cleanup
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // --- merged encodings ---

            // write brick starts buffer
            size_t complete_brickstarts_size = total_brick_count + 1;
            file.write(reinterpret_cast<const char *>(&complete_brickstarts_size), sizeof(size_t));
            if (complete_brickstarts_size * sizeof(uint32_t) != std::filesystem::file_size(brickstarts_path)) {
                Logger(Error) << "Brickstarts size " << std::filesystem::file_size(brickstarts_path) << " does not match the expected size " << (complete_brickstarts_size * sizeof(uint32_t));
                file.close();
                return nullptr;
            }
            std::ifstream brickstarts_file_in(brickstarts_path, std::ios_base::in | std::ios::binary);
            if (!brickstarts_file_in.is_open()) {
                Logger(Error) << "Unable to open file " << brickstarts_path << " for read. Skipping.";
                file.close();
                return nullptr;
            }
            file << brickstarts_file_in.rdbuf();
            brickstarts_file_in.close();

            // write number of split encoding buffers, all split encodings, and index to split array mapping
            std::ifstream encoding_file_in(encoding_path, std::ios_base::in | std::ios::binary);
            if (!encoding_file_in.is_open()) {
                Logger(Error) << "Unable to open file " << encoding_path << " for read. Skipping.";
                file.close();
                return nullptr;
            }
            file.write(reinterpret_cast<const char *>(&split_encoding_count), sizeof(size_t));
            file << encoding_file_in.rdbuf();
            encoding_file_in.close();

            // we never use detail separation here
            bool use_detail_separation = false;
            file.write(reinterpret_cast<const char *>(&use_detail_separation), sizeof(bool));
            file.close();
        }

        // reimport complete CSGV file
        Logger(Info) << "Merging Compressed Segmentation Volume chunk files 100%. complete volume size " << str(complete_volume_dim) << "                ";

        // TODO: only if detail separation takes too long to perform on every import of the merged volume,
        //  perform detail separation here if requested and overwrite output file with separated detail.

        // everything is complete. we can clean up the tmp files and return the merged compressed segmentation volume after loading it from the hard drive
        std::filesystem::remove(brickstarts_path);
        std::filesystem::remove(encoding_path);
        // wait for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        std::shared_ptr<CompressedSegmentationVolume> full_csgv = std::make_shared<volcanite::CompressedSegmentationVolume>();
        bool reimport_success = full_csgv->importFromFile(output_csgv_path, false, true);
        if (!reimport_success) {
            Logger(Error) << "Error re-importing exported merged Compressed Segmentation Volume from "
                          << output_csgv_path;
            return nullptr;
        }

        return full_csgv;
    }
};

} // namespace volcanite
