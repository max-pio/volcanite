#pragma once

#include <string>
#include <unordered_set>

#include "vvv/util/csv_utils.hpp"
#include "vvv/util/Logger.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "vvv/volren/Volume.hpp"

#include <chrono>
#include <thread>


namespace vvv {


// Easy to use managing class for obtaining Compressed Segmentation Volumes (CSGV).
// The static createCompressedSegmentationVolume() method can be used to obtain a CSGV with the given parameters for a .hdf5 or .raw data set.
// If force_recompute is false, it will load a previously computed compression from the same location if possible.
// The overall time to compress a data set is mostly the time to load the original volume from the hard drive, especially in the case of compressed hdf5 files and chunked data.
//
// File names:
// The file names of the CSGV to store to or load from the hard drive are deterministically inferred from the original volume file name and the compression parameters.
// That way, any once compressed volume can be identified and potentially loaded from the hard drive if it is requested at another time.
//
// Chunked data:
// For large data sets that are split into multiple chunks of data, a formatted path with three {} placeholders and a maximum file index can be passed.
// The handler then tries to load all chunk files from (0,0,0) to the maximum index (inclusive) where all 'inner' chunks must have a volume dimension which is a
// multiple of the brick size. Each of these chunks is compressed and exported independently.
// Afterward, a merging step is carried out to create a single CSGV containing the whole data set.
// A data set that is not split into chunks can be seen as a data set that consists of only one chunk (0,0,0).
// For example, "vol_x{}_y{}_z{}" with a maximum index (3,1,4) will compress and merge all chunks [vol_x0_y0_z0, vol_x1_y0_z0, ... vol_x3_y1_z4] into one CSGV.
//
// Operation Frequencies:
// If rANS encoding is applied when compressing, a quick pre-pass for obtaining operation frequency tables is performed.
// The resulting tables are saved to a "*.csgv_freq" file to be loaded later.


class CompSegVolHandler {

private:
    /** Merges all Compressed Segmentation Volume files from the individually compressed chunks given by the formatted path and the file index into one Compressed Segmentation Volume.
     * The formatted_path can contain zero up to three {} placeholders that will be replaced with the respective indices from 0 up to max_file_index.x|y|z.
     * "test_{}_{}_{}" will be replaced up to "test_1_2_3" with max_file_index=(1,2,3)"
     */
    static std::shared_ptr<CompressedSegmentationVolume> mergeCompressedSegmentationVolumeChunksFromFiles(const std::string &formatted_path, glm::ivec3 max_file_index, int brick_dim,
                                                                                           CompressedSegmentationVolume::RANSMode rANS_mode, bool use_detail_separation) {
        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 0%";

        // our final filename
        std::string complete_path = CompressedSegmentationVolume::getCSGVFileName(combinedPath(formatted_path, max_file_index), brick_dim, rANS_mode, use_detail_separation);
        if (std::filesystem::exists(complete_path)) {
            Logger(WARN) << "File " << complete_path << " already exists! Will be overwritten.";
        }
        glm::uvec3 complete_volume_dim(0u);
        size_t complete_brickstarts_size = 0ul; // also by definition the complete_detailstarts_size
        size_t complete_encoding_size = 0ul;
        size_t complete_detail_size = 0ul;

        // we first start by creating two tmp files where we construct the combined brickstarts and encoding buffers
        std::string brickstarts_path = complete_path + "_brickstarts.tmp";
        if (std::filesystem::exists(brickstarts_path))
            Logger(WARN) << "Overwriting existing file " << brickstarts_path;
        std::ofstream brickstarts_file(brickstarts_path, std::ios_base::out | std::ios::binary);
        if (!brickstarts_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << brickstarts_path << ". Skipping.";
            return nullptr;
        }
        std::string encoding_path = complete_path + "_encoding.tmp";
        if (std::filesystem::exists(encoding_path))
            Logger(WARN) << "Overwriting existing file " << encoding_path;
        std::ofstream encoding_file(encoding_path, std::ios_base::out | std::ios::binary);
        if (!encoding_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << encoding_path << ". Skipping.";
            return nullptr;
        }

        std::string detailstarts_path = complete_path + "_detailstarts.tmp";
        std::ofstream detailstarts_file;
        std::string detail_path = complete_path + "_detail.tmp";
        std::ofstream detail_file;
        if(use_detail_separation) {
            if (std::filesystem::exists(detailstarts_path))
                Logger(WARN) << "Overwriting existing file " << detailstarts_path;
            detailstarts_file = std::ofstream(detailstarts_path, std::ios_base::out | std::ios::binary);
            if (!detailstarts_file.is_open()) {
                Logger(ERROR) << "Unable to open file " << detailstarts_path << ". Skipping.";
                return nullptr;
            }
            if (std::filesystem::exists(detail_path))
                Logger(WARN) << "Overwriting existing file " << detail_path;
            detail_file = std::ofstream(detail_path, std::ios_base::out | std::ios::binary);
            if (!detail_file.is_open()) {
                Logger(ERROR) << "Unable to open file " << detail_path << ". Skipping.";
                return nullptr;
            }
        }

        // now we iterate through y and z dimensions brick by brick, but through the x dimension chunk by chunk
        glm::uvec3 brick_index = glm::uvec3(0, 0, 0);
        glm::ivec3 last_chunk_index = glm::ivec3(-1, -1, -1);
        glm::ivec3 chunk_index = glm::ivec3(0, 0, 0);
        size_t brickstarts_offset = 0ul;
        size_t detailstarts_offset = 0ul;
        // we load all Compressed Segmentation Volumes in one X-line at once
        CompressedSegmentationVolume dt_line[max_file_index.x + 1];
        while (glm::all(glm::lessThanEqual(chunk_index, max_file_index))) {
            if (glm::any(glm::notEqual(chunk_index, last_chunk_index))) {
                static constexpr int NUM_READ_THREADS = 4;
                // read next "line" of chunks
                #pragma omp parallel for num_threads(4) default(none) shared(dt_line, formatted_path, max_file_index, chunk_index, brick_dim, rANS_mode, use_detail_separation)
                for (int x = 0; x <= max_file_index.x; x++) {
                    bool success = dt_line[x].importFromFile(
                        CompressedSegmentationVolume::getCSGVFileName(formatChunkPath(formatted_path, x, chunk_index.y, chunk_index.z), brick_dim, rANS_mode, use_detail_separation), false);
                    if(!success)
                        throw std::runtime_error("Could not load expected chunk for merging");
                    success =
                        (glm::any(glm::equal(glm::ivec3(x, chunk_index.y, chunk_index.z), max_file_index)) ||
                         glm::all(glm::equal(glm::uvec3(dt_line[x].getVolumeDim().x % brick_dim, dt_line[x].getVolumeDim().y % brick_dim, dt_line[x].getVolumeDim().z % brick_dim), glm::uvec3(0))));
                    if(!success)
                        throw std::runtime_error("Only the border chunks are allowed to have a volume dimension that is not a multiple of the brick size");
                }
                last_chunk_index = chunk_index;
            }
            // write the whole x-line out
            for (chunk_index.x = 0; chunk_index.x <= max_file_index.x; chunk_index.x++) {
                auto brick_count = dt_line[chunk_index.x].getBrickCount();
                auto brick_starts = dt_line[chunk_index.x].getBrickStarts();
                auto detail_starts = dt_line[chunk_index.x].getDetailStarts();
                uint32_t first_brick_index = CompressedSegmentationVolume::brick_to_1D(glm::uvec3(0u, brick_index.y, brick_index.z), brick_count);
                uint32_t first_brick_start = brick_starts->at(first_brick_index);
                uint32_t last_brick_index = 1u + CompressedSegmentationVolume::brick_to_1D(glm::uvec3(brick_count.x - 1u, brick_index.y, brick_index.z), brick_count);
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
                if(brickstarts_offset >= (1u << 31u))
                    throw std::runtime_error("Brick start indexing exceeds 32 bit domain!");
                if(detailstarts_offset >= (1u << 31u))
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
                if (chunk_index.y > max_file_index.y) {
                    chunk_index.y = 0u;
                    brick_index.z++;
                    if (brick_index.z >= dt_line[0].getBrickCount().z) {
                        brick_index.z = 0u;
                        chunk_index.z++;
                        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files " << std::fixed << std::setprecision(0)
                                           << std::min(0.95f, (100.f * static_cast<float>(chunk_index.z) / static_cast<float>(max_file_index.z + 1))) << "%";
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
        if (rANS_mode != CompressedSegmentationVolume::NO_RANS) {
            complete_frequency_table = dt_line[0].getCurrentFrequencyTable(); // we assume that all chunks were encoded using the same frequency tables, otherwise this won't work anyway
            if(rANS_mode == CompressedSegmentationVolume::DOUBLE_TABLE_RANS)
                complete_detail_frequency_table = dt_line[0].getCurrentDetailFrequencyTable();
        }
        for (int x = 0; x < max_file_index.x; x++) {
            dt_line[x].clear();
        }

        // now append all files together
        {
            Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 95%, create mega-file..";
            if (std::filesystem::exists(complete_path)) {
                // Logger(WARN) << "Overwriting existing file " << complete_path;
                std::filesystem::remove(complete_path);
            }

            // open output and input file streams
            std::ofstream file(complete_path, std::ios_base::out | std::ios::binary);
            if (!file.is_open()) {
                Logger(ERROR) << "Unable to open file " << complete_path << " for write. Skipping.";
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
            file.write(reinterpret_cast<char *>(&rANS_mode), sizeof(CompressedSegmentationVolume::RANSMode)); // since 0011
            if(rANS_mode != CompressedSegmentationVolume::NO_RANS) {  // since 0002
                for (int i = 0; i < 16; i++)
                    file.write(reinterpret_cast<char *>(&complete_frequency_table[i]), sizeof(uint32_t));
            }
            if(rANS_mode == CompressedSegmentationVolume::DOUBLE_TABLE_RANS) {
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
            Logger(INFO) << complete_brickstarts_size;
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

        std::this_thread::sleep_for(std::chrono::milliseconds(8000)); // wait for cleanup

        Logger(INFO) << "Merging Compressed Segmentation Volume chunk files 100%. complete volume size " << str(complete_volume_dim);
        std::shared_ptr<CompressedSegmentationVolume> dt = std::make_shared<vvv::CompressedSegmentationVolume>();
        bool reimport_success = dt->importFromFile(complete_path, false);
        if(!reimport_success)
            throw std::runtime_error("Error re-importing exported merged Compressed Segmentation Volume!");
        return dt;
    }

public:
    CompSegVolHandler() = default;

    static bool tryGetEmptyIDsFromFile(std::string url, std::unordered_set<uint32_t> &empty_ids) {
        std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
        if (!nrrd.is_open()) {
            Logger(ERROR) << " you can provide a file " << url << " containing one label ID per line to set these labels to zero / invisible.";
            return false;
        }

        empty_ids.clear();

        std::string line;
        // ToDo: replace empty IDs csv with a list containing one label entry per line. all those are set to zero.
        // first line contains csv header
        if (!std::getline(nrrd, line)) {
            nrrd.close();
            throw std::runtime_error("unexpected end of file in " + url);
        }
        // read all other lines containing [cellid],[celltype]
        uint32_t type, cell_id;
        while (std::getline(nrrd, line)) {
            auto pos = line.rfind(',');
            cell_id = static_cast<uint32_t>(std::stol(line.substr(0, pos)));
            type = static_cast<uint32_t>(std::stol(line.substr(pos + 1, std::string::npos)));

            // empty is type <= 6
            if (type == 3 || type == 4)
                empty_ids.insert(cell_id);
        }

        nrrd.close();
        return true;
    }

    static std::string formatChunkPath(const std::string& formatted_path, int x, int y, int z) {
        std::string path = formatted_path;
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(x));
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(y));
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(z));
        return path;
    }

    static std::string combinedPath(const std::string& formatted_path, glm::ivec3 max_file_index) {
        if (glm::all(glm::equal(max_file_index, glm::ivec3(0, 0, 0)))) {
            std::string path = formatted_path;
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            return path;
        } else {
            std::string path = formatted_path;
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index.x));
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index.y));
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index.z));
            return path;
        }
    }

    static void loadSegmentationVolumeFile(std::string path, std::shared_ptr<Volume<uint32_t>>& volume) {
        if (path.back() == 'w')
            volume = Volume<uint32_t>::load_simple_cellsinsilico(path);
        else if (path.back() == '5')
            volume = Volume<uint32_t>::load_hdf5(path);
        else if (path.back() == 'i')
            volume = Volume<uint32_t>::load_vti(path);
        else
            throw std::runtime_error("Segmentation volume filetype not supported!");
    }



    static std::shared_ptr<CompressedSegmentationVolume> createCompressedSegmentationVolume(const std::string& formatted_path, int brick_dim = 32,
                                                                             CompressedSegmentationVolume::RANSMode rANS_mode = CompressedSegmentationVolume::DOUBLE_TABLE_RANS,
                                                              bool use_detail_separation = false, bool force_recompute = false, glm::uvec3 max_file_index = glm::uvec3(0u),
                                                              uint32_t freq_subsampling = 8u, bool verbose = true, std::string* latex_table_out_entry = nullptr) {

        if(use_detail_separation && rANS_mode != CompressedSegmentationVolume::DOUBLE_TABLE_RANS)
            throw std::runtime_error("Detail separation can only be used in combination with double table rANS!");
        if(freq_subsampling == 0u)
            throw std::runtime_error("Frequency subsampling must be at least 1 (= no subsampling)!");

        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        glm::ivec3 volume_dim(0);

        double total_freq_prepass_seconds = 0.f;
        double total_encoding_seconds = 0.f;

        std::string complete_path = combinedPath(formatted_path, max_file_index);
        std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<vvv::CompressedSegmentationVolume>();
        // check if we can load a precomputed compressed segmentation volume
        if (!force_recompute && csgv->importFromFile(CompressedSegmentationVolume::getCSGVFileName(complete_path, brick_dim, rANS_mode, use_detail_separation), true)) {
#ifdef RUN_TEST
            if (glm::all(glm::equal(max_file_index, glm::uvec3(0, 0, 0)))) {
                loadSegmentationVolumeFile(complete_path, volume);
                volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                Logger(INFO) << complete_path + " loaded with dim " << str(volume_dim);
                if (!csgv->test(volume->data(), volume_dim)) {
                    return nullptr;
                }
            } else {
                Logger(WARN) << "Testing not supported for pre-computed chunked data sets. Use force_recompute=true to do a full compression with a test per chunk.";
            }
#endif

#ifdef EXPORT_STATS
            Logger(DEBUG, true) << "export brick statistics...";
            std::string stats_path = csgv->getCSGVFileName(complete_path);
            //  csgv->exportBrickOperationsToCSV(stats_path.substr(0, stats_path.length() - 4) + "_example_brick.csv",
            //  (csgv->getBrickCount().x * csgv->getBrickCount().y * csgv->getBrickCount().z) / 2);
            stats_path = stats_path.substr(0, stats_path.length() - 4) + "_brickstats.csv";
            csv_export(csgv->gatherBrickStatistics(), stats_path);
            Logger(DEBUG) << "export brick statistics to " << stats_path + " done";
#endif
            return csgv;
        }

        // if we use rANS, we need to get a global frequency table shared over all chunks
        glm::uvec3 complete_volume_dim(0u);
        std::vector<size_t> code_frequencies(16, 0u);
        std::vector<size_t> detail_code_frequencies(16, 0u);
        if (rANS_mode != CompressedSegmentationVolume::NO_RANS) {
            // We may have a precomputed frequency table.
            // As operation frequencies do not change between rANS in single table or no rANS mode, we could use the same filename to store precomputed freq. tables in both cases.
            std::string freq_path = CompressedSegmentationVolume::getCSGVFileName(complete_path, brick_dim, rANS_mode, use_detail_separation) + "_freq";
            if (!force_recompute && std::filesystem::exists(freq_path)) {
                Logger(DEBUG) << "Use code frequencies from file " << freq_path;
                std::ifstream freq_file(freq_path, std::ios_base::in | std::ios::binary);
                if (!freq_file.is_open()) {
                    Logger(ERROR) << "Unable to open file " << freq_path << ". Skipping.";
                    return nullptr;
                }
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                freq_file.close();
            } else {
                Logger(DEBUG) << "Code frequency pass:";
                for (int z = 0; z <= max_file_index.z; z+=2) {   // @TODO: HARDCODED FREQUENCY SUBSAMPLING
                    for (int y = 0; y <= max_file_index.y; y+=2) {
                        for (int x = 0; x <= max_file_index.x; x+=2) {
                            // create new file path
                            std::string path = formatChunkPath(formatted_path, x, y, z);

                            loadSegmentationVolumeFile(path, volume);
                            volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                            // set all cells with an "invisible" cell type to 0
// ToDo: remove SET_EMPTY_TO_ZERO macro, or replace it with reading a line CSV containing JUST the empty IDs
#ifdef SET_EMPTY_TO_ZERO
                            std::unordered_set<uint32_t> empty_ids;
                            if (tryGetEmptyIDsFromFile(path + "_celltypes.csv", empty_ids)) {
                                Logger(INFO) << " " << path + " set empty cell ids to zero";

                                size_t volume_size = volume->size();
                                uint32_t *data = reinterpret_cast<uint32_t *>(volume->getRawData());

                                #pragma omp parallel for default(none) shared(data, empty_ids, volume_size)
                                for (int i = 0; i < volume_size; i++) {
                                    if (empty_ids.contains(data[i]))
                                        data[i] = 0u;
                                }
                            }
#endif
                            size_t tmp_code_frequencies[32];
                            csgv->setCompressionOptions(brick_dim, CompressedSegmentationVolume::NO_RANS);
                            csgv->compressForFrequencyTable(volume->data(), volume_dim, tmp_code_frequencies, freq_subsampling, rANS_mode == CompressedSegmentationVolume::DOUBLE_TABLE_RANS, false);
                            for (int i = 0; i < 16; i++) {
                                code_frequencies[i] += tmp_code_frequencies[i];
                                detail_code_frequencies[i] += tmp_code_frequencies[i+16];
                            }
                            total_freq_prepass_seconds += csgv->getLastTotalFreqPrepassSeconds();
                        }
                    }
                }

                // we can't risk missing symbol frequencies >0 in our table due to subsampling
                if(freq_subsampling > 1u) {
                    bool changed = false;
                    for(int i = 0; i < 16; i++) {
                        if (code_frequencies[i] == 0ul) {
                            changed = true;
                            code_frequencies[i] = 1ul;
                        }
                        if(rANS_mode == CompressedSegmentationVolume::DOUBLE_TABLE_RANS && detail_code_frequencies[i] == 0ul) {
                            changed = true;
                            detail_code_frequencies[i] = 1ul;
                        }
                    }
                    if (changed)
                        Logger(WARN) << " set zero frequency to 1 to avoid missing symbols because of frequency pass subsampling.";
                }

                // Write some general info about the chunk to a file (as of now, only the code frequencies)
                std::string freq_path = CompressedSegmentationVolume::getCSGVFileName(complete_path, brick_dim, rANS_mode, use_detail_separation) + "_freq";
                if (std::filesystem::exists(freq_path))
                    Logger(WARN) << "Overwriting existing file " << freq_path;
                std::ofstream freq_file(freq_path, std::ios_base::out | std::ios::binary);
                if (!freq_file.is_open()) {
                    Logger(ERROR) << "Unable to open file " << freq_path << ". Skipping.";
                    return nullptr;
                }
                for (int i = 0; i < 16; i++)
                    freq_file.write(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                for (int i = 0; i < 16; i++)
                    freq_file.write(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                freq_file.close();
            }

            if (verbose) {
                Logger(DEBUG) << "frequencies: " << arrayToString(code_frequencies.data(), code_frequencies.size())
                              << " detail frequencies: " << arrayToString(detail_code_frequencies.data(), detail_code_frequencies.size());
            }
            Logger(DEBUG) << "";
            Logger(DEBUG) << "";
            Logger(DEBUG) << "Compression pass:";
        }

        // now we encode every chunk on its own and store the result on the hard drive
        for (int z = 0; z <= max_file_index.z; z++) {
            for (int y = 0; y <= max_file_index.y; y++) {
                for (int x = 0; x <= max_file_index.x; x++) {

                    // create new file path
                    std::string path = formatChunkPath(formatted_path, x, y, z);
                    bool recompute = force_recompute || (max_file_index.x + max_file_index.y + max_file_index.z == 0u)      // if this is just one chunk, we also have to recompute at this point
                                     || !csgv->importFromFile(CompressedSegmentationVolume::getCSGVFileName(path, brick_dim, rANS_mode, use_detail_separation));
                    if (recompute) {
                        loadSegmentationVolumeFile(path, volume);
                        volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                        if (verbose) {
                            Logger(INFO) << " " << path + " loaded with dim " << str(volume_dim);
                            Logger(INFO) << "Running Encoding  -------------------------------------------";
                        }

                        // set all cells with an "invisible" cell type to 0
// ToDo: remove SET_EMPTY_TO_ZERO macro, or replace it with reading a line CSV containing JUST the empty IDs
#ifdef SET_EMPTY_TO_ZERO
                        std::unordered_set<uint32_t> empty_ids;
                        if (tryGetEmptyIDsFromFile(path + "_celltypes.csv", empty_ids)) {
                            Logger(INFO) << " " << path + " set empty cell ids to zero";

                            size_t volume_size = volume->size();
                            uint32_t *data = reinterpret_cast<uint32_t *>(volume->getRawData());

#pragma omp parallel for default(none) shared(data, empty_ids, volume_size)
                            for (int i = 0; i < volume_size; i++) {
                                if (empty_ids.contains(data[i]))
                                    data[i] = 0u;
                            }
                        }
#endif
                        // do the actual compression
                        csgv->setCompressionOptions64(brick_dim, rANS_mode, code_frequencies.data(), detail_code_frequencies.data());
                        csgv->compress(volume->data(), volume_dim, verbose);
                        if(use_detail_separation)
                            csgv->separateDetail();
                        total_encoding_seconds += csgv->getLastTotalEncodingSeconds();
                        if (std::filesystem::exists(csgv->getCSGVFileName(path))) {
                            if(!force_recompute)
                                Logger(WARN) << "overwriting file " << csgv->getCSGVFileName(path);
                            std::filesystem::remove(csgv->getCSGVFileName(path));
                        }
#ifdef RUN_TEST
                        if (!csgv->test(volume->data(), volume_dim)) {
                            return nullptr;
                        }
#endif
                        csgv->exportToFile(csgv->getCSGVFileName(path));
                    } else {
                        if (verbose) {
                            Logger(DEBUG) << csgv->decodingInfoString();
                            Logger(DEBUG) << "------------------------------------";
                        } else {
                            Logger(DEBUG) << path << " " << csgv->decodingInfoString();
                        }
#ifdef RUN_TEST
                        if (!volume) {
                            loadSegmentationVolumeFile(path, volume);
                            volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                            Logger(INFO) << path + " loaded with dim " << str(volume_dim);
                        }
                        if (!csgv->test(volume->data(), volume_dim)) {
                            return nullptr;
                        }
#endif
                    }

#ifdef EXPORT_STATS
                    Logger(DEBUG, true) << "export brick statistics...";
                    std::string stats_path = csgv->getCSGVFileName(path);
                    //  csgv->exportBrickOperationsToCSV(stats_path.substr(0, stats_path.length() - 4) + "_example_brick.csv",
                    //  (csgv->getBrickCount().x * csgv->getBrickCount().y * csgv->getBrickCount().z) / 2);
                    stats_path = stats_path.substr(0, stats_path.length() - 4) + "_brickstats.csv";
                    csv_export(csgv->gatherBrickStatistics(), stats_path);
                    Logger(DEBUG) << "export brick statistics to " << stats_path + " done";
#endif
                }
            }
        }

        // if we have multiple chunks, we have to merge them
        Logger(INFO) << "Total time: " << std::setprecision(3) << total_freq_prepass_seconds << " + " << total_encoding_seconds << " = " << (total_freq_prepass_seconds + total_encoding_seconds) << " seconds.";
        if (glm::any(glm::greaterThan(max_file_index, glm::uvec3(0)))) {
            // Log the total encoding times to a file
            csgv = mergeCompressedSegmentationVolumeChunksFromFiles(formatted_path, max_file_index, brick_dim, rANS_mode, use_detail_separation);
        }

        // create a latex table entry with the format | CR (%) | Time (s) | GB/s | encoded GB |
        if(latex_table_out_entry) {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3);
            ss << csgv->getCompressionRatio() << "\\% & " << (total_freq_prepass_seconds + total_encoding_seconds) << " & ";
            size_t total_volume_byte_size =
                csgv->getVolumeDim().x * csgv->getVolumeDim().y * csgv->getVolumeDim().z * sizeof(uint32_t) * (max_file_index.x + 1) *
                                            (max_file_index.y + 1) * (max_file_index.z + 1);
            ss << (static_cast<float>(total_volume_byte_size) / 1000.f / 1000.f / 1000.f / (total_freq_prepass_seconds + total_encoding_seconds)) << " & ";
            ss << csgv->getCompressedSizeInGB();
            *latex_table_out_entry = ss.str();
        }

        Logger(INFO) << "Total info: " << csgv->decodingInfoString();
        std::ofstream file(csgv->getCSGVFileName(complete_path) + ".log", std::ios_base::out);
        if (!file.is_open()) {
            Logger(ERROR) << "Unable to open file " << complete_path << ".log. Skipping.";
        } else {
            file << "Freq. prepass: " << total_freq_prepass_seconds << "s" << std::endl;
            file << "Encoding: " << total_encoding_seconds << "s" << std::endl;
            file << (total_freq_prepass_seconds + total_encoding_seconds) << std::endl;
            file << csgv->decodingInfoString() << std::endl;
            file.close();
        }

        return csgv;
    }

};

}

