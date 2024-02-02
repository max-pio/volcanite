#pragma once

#include <string>
#include <unordered_set>

#include "vvv/util/csv_utils.hpp"
#include "vvv/util/Logger.hpp"
#include <vvv/util/Paths.hpp>

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "vvv/volren/Volume.hpp"

#include <chrono>
#include <thread>

#define RELABEL_IDS_FROM_CSV_SUFFIX "_relabel.csv"

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
    static std::shared_ptr<CompressedSegmentationVolume> mergeCompressedSegmentationVolumeChunksFromFiles(const std::string& complete_csgv_path, const std::string& chunk_output_path_template, glm::ivec3 max_file_index, int brick_dim,
                                                                                                          CompressedSegmentationVolume::RANSMode rANS_mode, bool use_detail_separation, uint32_t cpu_threads) {
        Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 0%";

        // our final filename
        if (std::filesystem::exists(complete_csgv_path)) {
            Logger(WARN) << "File " << complete_csgv_path << " already exists! Will be overwritten.";
        }
        glm::uvec3 complete_volume_dim(0u);
        size_t complete_brickstarts_size = 0ul; // also by definition the complete_detailstarts_size
        size_t complete_encoding_size = 0ul;
        size_t complete_detail_size = 0ul;

        // we first start by creating two tmp files where we construct the combined brickstarts and encoding buffers
        std::string brickstarts_path = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_brickstarts.tmp";
        if (std::filesystem::exists(brickstarts_path))
            Logger(WARN) << "Overwriting existing file " << brickstarts_path;
        std::ofstream brickstarts_file(brickstarts_path, std::ios_base::out | std::ios::binary);
        if (!brickstarts_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << brickstarts_path << ". Skipping.";
            return nullptr;
        }
        std::string encoding_path = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_encoding.tmp";
        if (std::filesystem::exists(encoding_path))
            Logger(WARN) << "Overwriting existing file " << encoding_path;
        std::ofstream encoding_file(encoding_path, std::ios_base::out | std::ios::binary);
        if (!encoding_file.is_open()) {
            Logger(ERROR) << "Unable to open file " << encoding_path << ". Skipping.";
            return nullptr;
        }

        std::string detailstarts_path = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_detailstarts.tmp";
        std::ofstream detailstarts_file;
        std::string detail_path = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_detail.tmp";
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
        for(int i=0; i<max_file_index.x + 1; i++)
            dt_line[i].setCPUThreadCount(cpu_threads);
        while (glm::all(glm::lessThanEqual(chunk_index, max_file_index))) {
            if (glm::any(glm::notEqual(chunk_index, last_chunk_index))) {
                const int NUM_READ_THREADS = cpu_threads < 4 ? cpu_threads : 4;
                // read next "line" of chunks
                #pragma omp parallel for num_threads(NUM_READ_THREADS) default(none) shared(dt_line, chunk_output_path_template, max_file_index, chunk_index, brick_dim, rANS_mode, use_detail_separation)
                for (int x = 0; x <= max_file_index.x; x++) {

                    bool success = dt_line[x].importFromFile(formatChunkPath(chunk_output_path_template, x, chunk_index.y, chunk_index.z), false);
                    if(!success) {
                        std::string _err =  "Could not load expected chunk for merging from file " + formatChunkPath(chunk_output_path_template, x, chunk_index.y, chunk_index.z);
                        throw std::runtime_error(_err);
                    }
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
                auto detail_starts = use_detail_separation ? dt_line[chunk_index.x].getDetailStarts() : nullptr;
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
            Logger(INFO, true) << "Merging Compressed Segmentation Volume chunk files 95%, creating single file with complete volume..";
            if (std::filesystem::exists(complete_csgv_path)) {
                // Logger(WARN) << "Overwriting existing file " << complete_path;
                std::filesystem::remove(complete_csgv_path);
            }

            // open output and input file streams
            std::ofstream file(complete_csgv_path, std::ios_base::out | std::ios::binary);
            if (!file.is_open()) {
                Logger(ERROR) << "Unable to open file " << complete_csgv_path << " for writing. Skipping.";
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
        bool reimport_success = dt->importFromFile(complete_csgv_path, false);
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

    static bool relabelVoxelsFromCSV(std::string url, std::unordered_map<uint32_t, uint32_t> &type_per_id) {
        std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
        if (!nrrd.is_open()) {
            return false;
        }

        type_per_id.clear();

        std::string line;
        // first line contains csv header
        if (!std::getline(nrrd, line)) {
            nrrd.close();
            throw std::runtime_error("unexpected end of file in " + url);
        }
        // read all other lines containing [cellid],[celltype]
        uint32_t new_label, cell_id;
        while (std::getline(nrrd, line)) {
            auto pos = line.rfind(',');
            cell_id = static_cast<uint32_t>(std::stol(line.substr(0, pos)));
            new_label = static_cast<uint32_t>(std::stol(line.substr(pos + 1, std::string::npos)));

            type_per_id[cell_id] = new_label;
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
        if (path.ends_with(".raw"))
            volume = Volume<uint32_t>::load_simple_cellsinsilico(path);
        else if (path.ends_with(".hdf5"))
            volume = Volume<uint32_t>::load_hdf5(path);
        else if (path.ends_with(".vti"))
            volume = Volume<uint32_t>::load_vti(path);
        else {
            std::string _msg = "Segmentation volume filetype of " + path + " not supported!";
            throw std::runtime_error(_msg.c_str());
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

#ifdef RELABEL_IDS_FROM_CSV_SUFFIX
        std::unordered_map<uint32_t, uint32_t> id_types;
        if (relabelVoxelsFromCSV(path + RELABEL_IDS_FROM_CSV_SUFFIX, id_types)) {
            Logger(INFO) << "  relabeling ids from " << path << RELABEL_IDS_FROM_CSV_SUFFIX;
            size_t volume_size = volume->size();
            uint32_t *data = reinterpret_cast<uint32_t *>(volume->getRawData());
            #pragma omp parallel for default(none) shared(data, id_types, volume_size)
            for (int i = 0; i < volume_size; i++) {
                data[i] = id_types[data[i]];
            }
        }
#endif


    }


    static std::shared_ptr<CompressedSegmentationVolume> createCompressedSegmentationVolume(const std::string& input_path,
                                                                                            const std::string& output_path = "", int brick_dim = 32,
                                                                                            CompressedSegmentationVolume::RANSMode rANS_mode = CompressedSegmentationVolume::DOUBLE_TABLE_RANS,
                                                                                            uint32_t cpu_threads = 0u, bool use_detail_separation = false, bool force_recompute = false,
                                                                                            bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u),
                                                                                            uint32_t freq_subsampling = 8u, bool verbose = true, std::string* latex_table_out_entry = nullptr) {

        if (cpu_threads == 0u)
            cpu_threads = std::thread::hardware_concurrency();


        if(use_detail_separation && rANS_mode != CompressedSegmentationVolume::DOUBLE_TABLE_RANS)
            throw std::runtime_error("Detail separation can only be used in combination with double table rANS!");
        if(freq_subsampling == 0u)
            throw std::runtime_error("Frequency subsampling must be at least 1 (= no subsampling)!");
        if(use_detail_separation)
            Logger(WARN) << "Using detail separation is not recommended at compression stage and will be removed later";

        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        glm::ivec3 volume_dim(0);

        const bool create_log_file = true;
        const bool create_operation_freq_file = chunked_input_data;
        double total_freq_prepass_seconds = 0.f;
        double total_encoding_seconds = 0.f;

        MiniTimer total_encoding_import_export_timer;

        // determine output path for the complete volume
        std::string complete_csgv_path;
        bool use_temporary_output_file = output_path.empty();
        if(use_temporary_output_file) {
            // construct a temporary .csgv output path if no output path was specified
            // ToDo: try to use the location of the input file for temp csgv output files
            create_directory(std::filesystem::temp_directory_path() / "vvv");
            complete_csgv_path = (std::filesystem::temp_directory_path() / "vvv" / "tmp.csgv").string();
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }
        else {
            complete_csgv_path = output_path;
        }
        if(!complete_csgv_path.ends_with(".csgv")) {
            throw std::runtime_error("Output file must end with .csgv!");
        }

        // Compressing a chunked file can take a long time. We export all independently compressed chunks first, given
        // this file name template (creates a path like my/path/tmp_x{}_y{}_z{}_bs64_rANS2.csgv for example):
        std::string chunk_output_path_template = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_x{}_y{}_z{}.csgv";
        std::string chunk_output_path_template_no_separation = CompressedSegmentationVolume::getCSGVFileName(chunk_output_path_template, brick_dim, rANS_mode, false);
        chunk_output_path_template = CompressedSegmentationVolume::getCSGVFileName(chunk_output_path_template, brick_dim, rANS_mode, use_detail_separation);


        if(verbose) {
            Logger(INFO) << "Compressing " << input_path <<
            (chunked_input_data ? " with chunk indices" + str(max_file_index) : "") << " to " << complete_csgv_path <<
            " [b=" << brick_dim << ", s=" << rANS_mode << "]" << (use_detail_separation ? " with lod separation" : "");

        }

        std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<vvv::CompressedSegmentationVolume>();
        csgv->setCPUThreadCount(cpu_threads);
        // check if we can load a precomputed compressed segmentation volume
        if (!force_recompute && csgv->importFromFile(complete_csgv_path, false)) {
#ifdef RUN_TEST
            if (!chunked_input_data || glm::all(glm::equal(max_file_index, glm::uvec3(0, 0, 0)))) {
                loadSegmentationVolumeFile(complete_csgv_path, volume);
                volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                Logger(INFO) << complete_csgv_path + " loaded with dim " << str(volume_dim);
                if (!csgv->test(volume->data(), volume_dim)) {
                    return nullptr;
                }
            } else {
                Logger(WARN) << "Testing not supported for pre-computed chunked data sets. Use force_recompute=true to do a full compression with a test per chunk.";
            }
#endif

#ifdef EXPORT_STATS
            Logger(DEBUG, true) << "export brick statistics...";
            std::string stats_path = complete_csgv_path;
            stats_path = stats_path.substr(0, stats_path.length() - 5) + "_brickstats.csv";
            csv_export(csgv->gatherBrickStatistics(), stats_path);
            Logger(DEBUG) << "export brick statistics to " << stats_path + " done";
#endif
            Logger(INFO) << "Imported previously compressed file " << complete_csgv_path << ". Skipping compression.";
            return csgv;
        }

        // if we use rANS, we need to get a global frequency table shared over all chunks
        glm::uvec3 complete_volume_dim(0u);
        std::vector<size_t> code_frequencies(16, 0u);
        std::vector<size_t> detail_code_frequencies(16, 0u);
        if (rANS_mode != CompressedSegmentationVolume::NO_RANS) {
            // We may have a precomputed frequency table.
            // As operation frequencies do not change between rANS in single table or no rANS mode, we could use the same filename to store precomputed freq. tables in both cases.
            std::string freq_path = CompressedSegmentationVolume::getCSGVFileName(complete_csgv_path, brick_dim, rANS_mode, false, ".cfrq");
            if (!force_recompute && std::filesystem::exists(freq_path)) {
                Logger(DEBUG) << "using operation frequencies from file " << freq_path;
                std::ifstream freq_file(freq_path, std::ios_base::in | std::ios::binary);
                if (!freq_file.is_open()) {
                    Logger(ERROR) << "unable to open file " << freq_path << ". Aborting.";
                    return nullptr;
                }
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                freq_file.close();
            } else {
                Logger(DEBUG) << "operation frequency prepass:";
                // @ToDo: remove hardcoded frequency subsampling (+2) on a chunk level?
                for (int z = 0; z <= max_file_index.z; z+=2) {
                    for (int y = 0; y <= max_file_index.y; y+=2) {
                        for (int x = 0; x <= max_file_index.x; x+=2) {
                            // create new file path for the compressed version of this single chunk
                            std::string chunk_input_path = chunked_input_data ? formatChunkPath(input_path, x, y, z) : input_path;

                            loadSegmentationVolumeFile(chunk_input_path, volume);
                            volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);

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

                // Write some general info about the chunk to a file (as of now, only the operation frequencies)
                if(create_operation_freq_file) {
                    if (std::filesystem::exists(freq_path))
                        Logger(WARN) << "Overwriting existing file " << freq_path;
                    std::ofstream freq_file(freq_path, std::ios_base::out | std::ios::binary);
                    if (freq_file.is_open()) {
                        for (int i = 0; i < 16; i++)
                            freq_file.write(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                        for (int i = 0; i < 16; i++)
                            freq_file.write(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                        freq_file.close();
                    } else {
                        Logger(WARN) << "Unable to export operation frequencies to " << freq_path << ".";
                    }
                }
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

                    // create file input and output paths for this single chunk
                    std::string chunk_input_path = chunked_input_data ? formatChunkPath(input_path, x, y, z) : input_path;
                    std::string chunk_output_path = chunked_input_data ? formatChunkPath(chunk_output_path_template, x, y, z) : complete_csgv_path;

                    bool recompute = force_recompute || (max_file_index.x + max_file_index.y + max_file_index.z == 0u)      // if this is just one chunk, we also have to recompute at this point
                                     || !csgv->importFromFile(chunk_output_path, false);
                    // special case: we can load a volume without detail separation and THEN separate the detail (ToDo: this piece of code is a crime against humanity)
                    if(recompute && !force_recompute && (max_file_index.x + max_file_index.y + max_file_index.z != 0u) && use_detail_separation) {
                        // try to load the volume without detail separation
                        recompute = !csgv->importFromFile(formatChunkPath(chunk_output_path_template_no_separation, x, y, z), false);
                        // .. and separate detail on success
                        if(!recompute)
                            csgv->separateDetail();
                    }
                    if (recompute) {
#ifdef RELABEL_IDS_FROM_CSV_SUFFIX
                        if(!std::filesystem::exists(chunk_input_path + RELABEL_IDS_FROM_CSV_SUFFIX)) {
                            Logger(INFO) << "You can provide a file " << chunk_input_path << RELABEL_IDS_FROM_CSV_SUFFIX
                                         << " with the following format to relabel voxels:\n";
                            Logger(INFO) << "# One Line Header (first line will be ignored)";
                            Logger(INFO) << "[OldLabel0],[NewLabel0]";
                            Logger(INFO) << "[OldLabel1],[NewLabel1]";
                            Logger(INFO) << "...\n";
                        }
#endif

                        loadSegmentationVolumeFile(chunk_input_path, volume);
                        volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                        if (verbose) {
                            Logger(INFO) << " " << chunk_input_path + " loaded with dim " << str(volume_dim);
                            Logger(INFO) << "Running Encoding  --------------------------------------------";
                        }

                        // do the actual compression
                        csgv->setCompressionOptions64(brick_dim, rANS_mode, code_frequencies.data(), detail_code_frequencies.data());
                        csgv->compress(volume->data(), volume_dim, verbose);
                        // ToDo: remove detail separation at this point. It should only be a method of the csgv volume after creation as it is only needed for rendering on certain systems.
                        if(use_detail_separation) {
                            csgv->separateDetail();
                        }
                        total_encoding_seconds += csgv->getLastTotalEncodingSeconds();
                        if (std::filesystem::exists(chunk_output_path)) {
                            Logger(WARN) << "overwriting file " << chunk_output_path;
                            std::filesystem::remove(chunk_output_path);
                        }
#ifdef RUN_TEST
                        if (!csgv->test(volume->data(), volume_dim)) {
                            return nullptr;
                        }
#endif
                        csgv->exportToFile(chunk_output_path);
                    } else {
                        if (verbose) {
                            Logger(INFO) << " reusing existing csgv file " << chunk_output_path << " " << csgv->decodingInfoString();
                        } else {
                            Logger(INFO) << " reusing existing csgv file " << chunk_output_path;
                        }
#ifdef RUN_TEST
                        if (!volume) {
                            loadSegmentationVolumeFile(chunk_input_path, volume);
                            volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                            Logger(INFO) << chunk_input_path + " loaded with dim " << str(volume_dim);
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
        Logger(INFO) << "Total raw compression time: " << std::setprecision(3) << total_freq_prepass_seconds << " + "
        << total_encoding_seconds << " = " << (total_freq_prepass_seconds + total_encoding_seconds) << "s, "
        << "including file IO: " << total_encoding_import_export_timer.elapsed() << "s.";
        if (chunked_input_data && glm::any(glm::greaterThan(max_file_index, glm::uvec3(0)))) {
            // Log the total encoding times to a file
            csgv = mergeCompressedSegmentationVolumeChunksFromFiles(complete_csgv_path, chunk_output_path_template, max_file_index, brick_dim, rANS_mode, use_detail_separation, cpu_threads);
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
        // create a log file
        if(create_log_file) {
            std::ofstream file(csgv->getCSGVFileName(complete_csgv_path) + ".log", std::ios_base::out);
            if (!file.is_open()) {
                Logger(ERROR) << "Unable to open file " << complete_csgv_path << ".log. Skipping.";
            } else {
                file << MiniTimer::getCurrentDateTime() << std::endl;
                file << "Compression time [s] excluding file import and export:" << std::endl;
                file << "  Frequency prepass: " << total_freq_prepass_seconds << "s" << std::endl;
                file << "   Compression pass: " << total_encoding_seconds << "s" << std::endl;
                file << "  Total compression: " << (total_freq_prepass_seconds + total_encoding_seconds) << std::endl;
                file << "" << std::endl;
                file << "Compressed volume information:" << std::endl;
                file << "  " << csgv->decodingInfoString() << std::endl;
                file.close();
            }
        }

        // remove all temporary files created during the compression
        if (chunked_input_data && glm::any(glm::greaterThan(max_file_index, glm::uvec3(0)))) {
            for (int z = 0; z <= max_file_index.z; z++) {
                for (int y = 0; y <= max_file_index.y; y++) {
                    for (int x = 0; x <= max_file_index.x; x++) {
                        std::string chunk_output_path = formatChunkPath(chunk_output_path_template, x, y, z);
                        if (std::filesystem::exists(chunk_output_path))
                            std::filesystem::remove(chunk_output_path);
                    }
                }
            }
            std::string s;
            s = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_brickstarts.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_detailstarts.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_encoding.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = complete_csgv_path.substr(0, complete_csgv_path.length() - 5) + "_detail.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
        }
        if(use_temporary_output_file) {
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }

        return csgv;
    }

};

}

