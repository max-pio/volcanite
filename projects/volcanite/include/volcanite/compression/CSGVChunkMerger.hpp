#pragma once

#include <memory>

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

namespace vvv {

class CSGVChunkMerger {
private:
    // full volume properties
    glm::ivec3 chunk_count;
    uint32_t total_chunk_count;
    glm::uvec3 brick_count;
    size_t total_brick_count;

    // (inner) chunk properties
    glm::uvec3 chunk_dimension;
    uint32_t brick_size;
    RANSMode rANS_mode;
    std::vector<uint32_t> complete_frequency_table;
    std::vector<uint32_t> complete_detail_frequency_table;


public:
    CSGVChunkMerger() = default;

    /** Merges all CompressedSegmentationVolume files from the individually compressed chunks given by a formatted path and maximum file index into one CompressedSegmentationVolume.
     * The formatted_path can contain zero up to three {} placeholders that will be replaced with the respective indices from 0 up to max_file_index.x|y|z, e.g:
     * "test_{}_{}_{}" will be replaced up to "test_1_2_3" with max_file_index=(1,2,3)".
     * All input chunk csgv files must use the same brick size, rANS mode, and rANS frequency tables and must not use detail separation.
     * Any input chunk csgv file with chunk indices smaller than max_input_csgv_index must have a volume dimension that is evenly dividable by the brick size.
     * @param output_csgv_path output *.csgv file to which the merged tiles are exported
     * @param input_csgv_template_path formatted path of input chunk *.csgv files
     * @param max_input_csgv_index inclusive last x y z tile indices to merge
     */
    std::shared_ptr<CompressedSegmentationVolume> mergeCompressedSegmentationVolumeChunksFromFiles(const std::string& output_csgv_path,
                                                                                                          const std::string& input_csgv_template_path,
                                                                                                          glm::ivec3 max_input_csgv_index,
                                                                                                          uint32_t cpu_threads) {
        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 0%";

        // our final filename
        if (std::filesystem::exists(output_csgv_path)) {
            Logger(WARN) << "File " << output_csgv_path << " already exists! Will be overwritten.";
        }

        // 1. load all chunk CSGV files into memory
        chunk_count = max_input_csgv_index + glm::ivec3(1);
        total_chunk_count = chunk_count.x * chunk_count.y * chunk_count.z;
        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 0% (import " << total_chunk_count << " chunk csgv files..)";
        CompressedSegmentationVolume* chunks = new CompressedSegmentationVolume[total_chunk_count];
        for(uint32_t c = 0; c < total_chunk_count; c++) {
            glm::ivec3 chunk_index = sfc::Cartesian::i2p(c, chunk_count);
            chunks[c].importFromFile(formatChunkPath(input_csgv_template_path, chunk_index.x, chunk_index.y, chunk_index.z), false);

            if(c == 0) {
                // store parameters of chunks
                chunk_dimension = chunks[0].getVolumeDim();
                brick_size = chunks[0].getBrickSize();
                if(chunk_dimension.x % brick_size != 0 || chunk_dimension.x % brick_size != 0 || chunk_dimension.x % brick_size != 0) {
                    Logger(ERROR)
                            << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunk dimension must be multiple of brick size.";
                    delete[] chunks;
                    return nullptr;
                }
                rANS_mode = chunks[0].getRANSMode();
                if (rANS_mode != NO_RANS)
                    complete_frequency_table = chunks[0].getCurrentFrequencyTable();
                if(rANS_mode == DOUBLE_TABLE_RANS)
                    complete_detail_frequency_table = chunks[0].getCurrentDetailFrequencyTable();
            } else {
                // check if chunk CSGV use the same compression parameters
                if (rANS_mode != chunks[0].getRANSMode()) {
                    Logger(ERROR)
                            << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunks must use same rANS mode.";
                    delete[] chunks;
                    return nullptr;
                }
                if ((rANS_mode != NO_RANS && complete_frequency_table != chunks[c].getCurrentFrequencyTable()) ||
                    (rANS_mode == DOUBLE_TABLE_RANS &&
                     complete_detail_frequency_table != chunks[c].getCurrentDetailFrequencyTable())) {
                    Logger(ERROR)
                            << "Merging Compressed Segmentation Volume chunk files failed. Input CSGV chunks must use same rANS frequency tables.";
                    delete[] chunks;
                    return nullptr;
                }
                if (chunk_index.x < max_input_csgv_index.x && chunk_dimension.x != chunks[c].getVolumeDim().x ||
                    chunk_index.y < max_input_csgv_index.y && chunk_dimension.y != chunks[c].getVolumeDim().y ||
                    chunk_index.z < max_input_csgv_index.z && chunk_dimension.z != chunks[c].getVolumeDim().z) {
                    Logger(ERROR)
                            << "Merging Compressed Segmentation Volume chunk files failed. Inner CSGV chunks must have the same volume dimensions.";
                    delete[] chunks;
                    return nullptr;
                }
                // ToDo: check if volume dimensions of outer CSGV chunks fit
            }
        }
        glm::uvec3 complete_volume_dim = chunk_dimension * glm::uvec3(max_input_csgv_index) + chunks[sfc::Cartesian::p2i(max_input_csgv_index, chunk_count)].getVolumeDim();
        brick_count = (glm::uvec3(chunk_count - glm::ivec3(1)) * chunk_dimension + chunks[sfc::Cartesian::p2i(max_input_csgv_index, chunk_count)].getVolumeDim() - glm::uvec3(1)) / brick_size + 1u;
        total_brick_count = brick_count.x * brick_count.y * brick_count.z;
        if(total_brick_count > (1ul << 32) - 1ul) {
            Logger(ERROR)
                    << "Merging Compressed Segmentation Volume chunk files failed. Brick count exceeds 32 bit range. Use a larger brick size.";
            delete[] chunks;
            return nullptr;
        }
        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 0% (chunk import for " << str(complete_volume_dim) << " volume complete)";

        // 2. start by creating two tmp files where we construct the combined brickstarts and encoding buffers
        std::string brickstarts_path = output_csgv_path.substr(0, output_csgv_path.length() - 5) + "_brickstarts.tmp";
        if (std::filesystem::exists(brickstarts_path))
            Logger(WARN) << "Overwriting existing file " << brickstarts_path;
        std::ofstream brickstarts_file(brickstarts_path, std::ios_base::out | std::ios::binary);
        if (!brickstarts_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << brickstarts_path << ". Skipping.";
            delete[] chunks;
            return nullptr;
        }
        std::string encoding_path = output_csgv_path.substr(0, output_csgv_path.length() - 5) + "_encoding.tmp";
        if (std::filesystem::exists(encoding_path))
            Logger(WARN) << "Overwriting existing file " << encoding_path;
        std::ofstream encoding_file(encoding_path, std::ios_base::out | std::ios::binary);
        if (!encoding_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << encoding_path << ". Skipping.";
            brickstarts_file.close();
            delete[] chunks;
            return nullptr;
        }

        // 3. iterate over all output brick indices
        // a) get encoding memory area of brick from corresponding input chunk csgv
        // b) determine brick_idx_to_enc_vector / start new split encoding arrays in output file
        // c) write brick encoding to encoding tmp file
        // d) write brick start within current output encoding array to brickstarts tmp file
        size_t complete_brickstarts_size = 0ul; // also by definition the complete_detailstarts_size
        size_t complete_encoding_size = 0ul;
        // ToDo: write dummy encoding lengths / brick_idx_to_voxel_encoding using long brick_idx_to_voxel_encoding_pos = encoding_file.tellp(); and later encoding_file.seekp(pos);
        for(size_t brick_idx = 0u; brick_idx < total_brick_count; brick_idx++) {
            throw std::runtime_error("Continue CSGVMerger implementation here..");
        }

        // close all chunk CSGV files
        delete[] chunks;

        // append tmp files together to form one valid csgv file

        // reimport complete CSGV file

        // ToDo: only if detail separation takes too long to perform on every load, perform detail separation if requested and overwrite output file with separated detail





        // =============================================================================================================
        // =============================================================================================================
        // =============================================================================================================
        // =============================================================================================================

        // now we iterate through y and z dimensions brick by brick, but through the x dimension chunk by chunk
        glm::uvec3 brick_index = glm::uvec3(0, 0, 0);
        glm::ivec3 last_chunk_index = glm::ivec3(-1, -1, -1);
        glm::ivec3 chunk_index = glm::ivec3(0, 0, 0);
        size_t brickstarts_offset = 0ul;
        size_t detailstarts_offset = 0ul;
        // we load all Compressed Segmentation Volumes in one X-line at once
        CompressedSegmentationVolume dt_line[max_input_csgv_index.x + 1];
        for(int i=0; i < max_input_csgv_index.x + 1; i++)
            dt_line[i].setCPUThreadCount(cpu_threads);
        while (glm::all(glm::lessThanEqual(chunk_index, max_input_csgv_index))) {
            if (glm::any(glm::notEqual(chunk_index, last_chunk_index))) {
                const int NUM_READ_THREADS = cpu_threads < 4 ? cpu_threads : 4;
                // read next "line" of chunks
                #pragma omp parallel for num_threads(NUM_READ_THREADS) default(none) shared(dt_line, chunk_output_path_template, max_file_index, chunk_index, brick_dim, rANS_mode, use_detail_separation)
                for (int x = 0; x <= max_input_csgv_index.x; x++) {

                    bool success = dt_line[x].importFromFile(formatChunkPath(input_csgv_template_path, x, chunk_index.y, chunk_index.z), false);
                    if(!success) {
                        std::string _err =  "Could not load expected chunk for merging from file " + formatChunkPath(input_csgv_template_path, x, chunk_index.y, chunk_index.z);
                        throw std::runtime_error(_err);
                    }
                    success =
                        (glm::any(glm::equal(glm::ivec3(x, chunk_index.y, chunk_index.z), max_input_csgv_index)) ||
                         glm::all(glm::equal(glm::uvec3(dt_line[x].getVolumeDim().x % brick_dim, dt_line[x].getVolumeDim().y % brick_dim, dt_line[x].getVolumeDim().z % brick_dim), glm::uvec3(0))));
                    if(!success)
                        throw std::runtime_error("Only the border chunks are allowed to have a volume dimension that is not a multiple of the brick size");
                }
                last_chunk_index = chunk_index;
            }
            // write the whole x-line out
            for (chunk_index.x = 0; chunk_index.x <= max_input_csgv_index.x; chunk_index.x++) {
                auto brick_count = dt_line[chunk_index.x].getBrickCount();
                auto brick_starts = dt_line[chunk_index.x].getBrickStarts();
                auto detail_starts = use_detail_separation ? dt_line[chunk_index.x].getDetailStarts() : nullptr;
                uint32_t first_brick_index = CompressedSegmentationVolume::brick_pos2idx(
                        glm::uvec3(0u, brick_index.y, brick_index.z), brick_count);
                uint32_t first_brick_start = brick_starts->at(first_brick_index);
                uint32_t last_brick_index = 1u + CompressedSegmentationVolume::brick_pos2idx(
                        glm::uvec3(brick_count.x - 1u, brick_index.y, brick_index.z), brick_count);
                uint32_t last_brick_end = brick_starts->at(last_brick_index);

                uint32_t first_detail_start = use_detail_separation ? detail_starts->at(first_brick_index) : 0;
                uint32_t last_detail_end = use_detail_separation ? detail_starts->at(last_brick_index) : 0;

                // we handle all brick start indices in this line relatively (so x=0 has the brick start 0) but then add the total offset so far to this line
                uint32_t ofsetted_start;
                uint32_t ofsetted_detail_start;
                for (uint32_t bi = first_brick_index; bi < last_brick_index; bi++) {
                    ofsetted_start = brick_starts->at(bi) - first_brick_start + static_cast<uint32_t>(brickstarts_offset);
                    brickstarts_file.write(reinterpret_cast<const char *>(&ofsetted_start), sizeof(uint32_t));
                    if(use_detail_separation) {
                        ofsetted_detail_start = detail_starts->at(bi) - first_detail_start + static_cast<uint32_t>(detailstarts_offset);
                        detailstarts_file.write(reinterpret_cast<const char *>(&ofsetted_detail_start), sizeof(uint32_t));
                    }
                }
                brickstarts_offset += (brick_starts->at(last_brick_index) - first_brick_start);
                if(use_detail_separation)
                    detailstarts_offset += (detail_starts->at(last_brick_index) - first_detail_start);
                if(brickstarts_offset >= (~0u))
                    throw std::runtime_error("Brick start indexing exceeds 32 bit domain!");
                if(detailstarts_offset >= (~0u))
                    throw std::runtime_error("detailstarts indexing exceeds 32 bit domain!");
                encoding_file.write(reinterpret_cast<const char *>(&(dt_line[chunk_index.x].getEncoding()->at(first_brick_start))), (last_brick_end - first_brick_start) * sizeof(uint32_t));
                if(use_detail_separation)
                    detail_file.write(reinterpret_cast<const char *>(&(dt_line[chunk_index.x].getDetail()->at(first_detail_start))), (last_detail_end - first_detail_start) * sizeof(uint32_t));

                // track info
                complete_brickstarts_size += (last_brick_index - first_brick_index);
                complete_encoding_size += (last_brick_end - first_brick_start);
                complete_detail_size += (last_detail_end - first_detail_start);
                // uint32_t diff = brick_starts->at(last_brick_index) - brick_starts->at(last_brick_index);
                assert(brickstarts_offset == complete_encoding_size && "brick starts offsets don't match the encoding buffer size");
                if (chunk_index.y == 0 && chunk_index.z == 0 && brick_index.y == 0u && brick_index.z == 0u)
                    complete_volume_dim.x += dt_line[chunk_index.x].getVolumeDim().x;
            }
            if (chunk_index.z == 0 && brick_index.z == 0u && brick_index.y == 0u)
                complete_volume_dim.y += dt_line[0].getVolumeDim().y;
            if (chunk_index.y == 0 && brick_index.y == 0u && brick_index.z == 0u)
                complete_volume_dim.z += dt_line[0].getVolumeDim().z;

            // jesus take the wheel
            chunk_index.x = 0;
            brick_index.y++;
            if (brick_index.y >= dt_line[0].getBrickCount().y) {
                brick_index.y = 0u;
                chunk_index.y++;
                if (chunk_index.y > max_input_csgv_index.y) {
                    chunk_index.y = 0u;
                    brick_index.z++;
                    if (brick_index.z >= dt_line[0].getBrickCount().z) {
                        brick_index.z = 0u;
                        chunk_index.z++;
                        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files " << std::fixed << std::setprecision(0)
                                           << std::min(0.95f, (100.f * static_cast<float>(chunk_index.z) / static_cast<float>(max_input_csgv_index.z + 1))) << "%";
                    }
                }
            }
        }
        // write the last dummy brickstarts entry
        brickstarts_file.write(reinterpret_cast<const char *>(&complete_encoding_size), sizeof(uint32_t));
        complete_brickstarts_size++;
        brickstarts_file.close();
        if(use_detail_separation) {
            detailstarts_file.write(reinterpret_cast<const char *>(&complete_detail_size), sizeof(uint32_t));
            detailstarts_file.close();
            detail_file.close();
        }
        encoding_file.close();

        std::vector<uint32_t> complete_frequency_table;
        std::vector<uint32_t> complete_detail_frequency_table;
        if (rANS_mode != NO_RANS) {
            complete_frequency_table = dt_line[0].getCurrentFrequencyTable(); // we assume that all chunks were encoded using the same frequency tables, otherwise this won't work anyway
            if(rANS_mode == DOUBLE_TABLE_RANS)
                complete_detail_frequency_table = dt_line[0].getCurrentDetailFrequencyTable();
        }
        for (int x = 0; x < max_input_csgv_index.x; x++) {
            dt_line[x].clear();
        }

        // now append all files together
        {
            Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 95%, creating single file with complete volume..";
            if (std::filesystem::exists(output_csgv_path)) {
                // Logger(WARN) << "Overwriting existing file " << complete_path;
                std::filesystem::remove(output_csgv_path);
            }

            // open output and input file streams
            std::ofstream file(output_csgv_path, std::ios_base::out | std::ios::binary);
            if (!file.is_open()) {
                Logger(ERROR) << "Unable to open file " << output_csgv_path << " for writing. Skipping.";
                return nullptr;
            }

            // write header: 8 chars CMPSGVOL + 4 chars version number
            const char *magic_header = "CMPSGVOL";
            const char *version = "0011";
            file.write(magic_header, 8);
            file.write(version, 4);
            // write general info
            uint32_t brick_size = static_cast<uint32_t>(brick_dim);
            file.write(reinterpret_cast<char *>(&brick_size), sizeof(uint32_t));
            file.write(reinterpret_cast<char *>(&complete_volume_dim), sizeof(glm::uvec3));
            file.write(reinterpret_cast<char *>(&rANS_mode), sizeof(RANSMode)); // since 0011
            if(rANS_mode != NO_RANS) {  // since 0002
                for (int i = 0; i < 16; i++)
                    file.write(reinterpret_cast<char *>(&complete_frequency_table[i]), sizeof(uint32_t));
            }
            if(rANS_mode == DOUBLE_TABLE_RANS) {
                for (int i = 0; i < 16; i++)
                    file.write(reinterpret_cast<char *>(&complete_detail_frequency_table[i]), sizeof(uint32_t));
            }
            // write brick starts buffer through file streams
            std::ifstream brickstarts_file_in(brickstarts_path, std::ios_base::in | std::ios::binary);
            if (!brickstarts_file_in.is_open()) {
                Logger(ERROR) << "Unable to open file " << brickstarts_path << " for read. Skipping.";
                file.close();
                return nullptr;
            }
            size_t expected_brickstarts_size = ((complete_volume_dim.x - 1) / brick_dim + 1) * ((complete_volume_dim.y - 1) / brick_dim + 1) * ((complete_volume_dim.z - 1) / brick_dim + 1) + 1u;
            if (complete_brickstarts_size != expected_brickstarts_size) {
                Logger(WARN) << "Warning! brickstarts size " << complete_brickstarts_size << " doesn't match the expected size " << expected_brickstarts_size;
                assert(false && "merged brickstarts size doesn't match the expected size");
            }
            file.write(reinterpret_cast<char *>(&complete_brickstarts_size), sizeof(size_t));
            file << brickstarts_file_in.rdbuf();
            brickstarts_file_in.close();
            // write encoding buffer through files treams
            std::ifstream encoding_file_in(encoding_path, std::ios_base::in | std::ios::binary);
            if (!encoding_file_in.is_open()) {
                Logger(ERROR) << "Unable to open file " << encoding_path << " for read. Skipping.";
                return nullptr;
            }
            file.write(reinterpret_cast<char *>(&complete_encoding_size), sizeof(size_t));
            file << encoding_file_in.rdbuf();
            encoding_file_in.close();
            file.write(reinterpret_cast<char *>(&use_detail_separation), sizeof(bool));
            if(use_detail_separation) {
                // write detail starts buffer through file streams
                std::ifstream detailstarts_file_in(detailstarts_path, std::ios_base::in | std::ios::binary);
                if (!detailstarts_file_in.is_open()) {
                    Logger(ERROR) << "Unable to open file " << detailstarts_path << " for read. Skipping.";
                    file.close();
                    return nullptr;
                }
                // detail_starts buffer has the same size as the brick_starts buffer
                file.write(reinterpret_cast<char *>(&complete_brickstarts_size), sizeof(size_t));
                file << detailstarts_file_in.rdbuf();
                detailstarts_file_in.close();
                // write detail encoding buffer through file streams
                std::ifstream detail_file_in(detail_path, std::ios_base::in | std::ios::binary);
                if (!detail_file_in.is_open()) {
                    Logger(ERROR) << "Unable to open file " << detail_path << " for read. Skipping.";
                    return nullptr;
                }
                file.write(reinterpret_cast<char *>(&complete_detail_size), sizeof(size_t));
                file << detail_file_in.rdbuf();
                detail_file_in.close();
            }
            file.close();
        }

        // everything is complete. we can clean up the two tmp files and return the merged compressed segmentation volume after loading it from the hard drive
        std::filesystem::remove(detailstarts_path);
        std::filesystem::remove(detail_path);
        std::filesystem::remove(brickstarts_path);
        std::filesystem::remove(encoding_path);

        Logger(INFO) << "Merging Compressed Segmentation Volume chunk files 100%. complete volume size " << str(complete_volume_dim) << "                ";
        std::this_thread::sleep_for(std::chrono::milliseconds(4000)); // wait for cleanup

        std::shared_ptr<CompressedSegmentationVolume> dt = std::make_shared<vvv::CompressedSegmentationVolume>();
        dt->setCPUThreadCount(cpu_threads);
        bool reimport_success = dt->importFromFile(output_csgv_path, false, true);
        if(!reimport_success)
            throw std::runtime_error("Error re-importing exported merged Compressed Segmentation Volume!");

        return dt;
    }

};

}   // namespace vvv
