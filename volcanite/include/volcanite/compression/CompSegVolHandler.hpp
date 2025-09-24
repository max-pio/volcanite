//  Copyright (C) 2024, Max Piochowiak and Fabian Schiekel Karlsruhe Institute of Technology
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

#include <string>
#include <thread>
#include <unordered_set>

#include "vvv/util/Logger.hpp"
#include "vvv/util/csv_utils.hpp"
#include "vvv/volren/Volume.hpp"

#include "csgv_constants.incl"
#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/compression/CSGVChunkMerger.hpp"
#include "volcanite/util/segmentation_volume_synthesis.hpp"

#define RELABEL_IDS_FROM_CSV_SUFFIX "_relabel.csv"

using namespace vvv;

namespace volcanite {

/// Easy to use managing class for obtaining Compressed Segmentation Volumes (CSGV).
/// The createCompressedSegmentationVolume() method can be used to obtain a CSGV with the given parameters, e.g. for a .hdf5 or .nrrd data set.
/// If force_recompute is false, it will load a previously computed compression from the same location if possible.
/// The overall time to compress a data set is mostly the time to load the original volume from the hard drive, especially in the case of compressed hdf5 files.
/// \n\n
/// Chunked data:\n
/// For large data sets that are split into multiple chunks of data, a formatted path with three {} placeholders and a maximum file index can be passed.
/// The handler then tries to load all chunk files from (0,0,0) to the maximum index (inclusive) where all 'inner' chunks must have a volume dimension which is a
/// multiple of the brick size. Each of these chunks is compressed and exported independently.
/// Afterward, a merging step is carried out to create a single CSGV containing the whole data set.
/// A data set that is not split into chunks can be seen as a data set that consists of only one chunk (0,0,0).
/// For example, "vol_x{}_y{}_z{}" with a maximum index (3,1,4) will compress and merge all chunks [vol_x0_y0_z0, vol_x1_y0_z0, ... vol_x3_y1_z4] into one CSGV.
/// \n\n
/// Operation Frequencies:\n
/// If rANS encoding is applied when compressing, a quick pre-pass for obtaining operation frequency tables is performed.
class CompSegVolHandler {

    // TODO: split CompSegVolHandler in .hpp and .cpp definitions

  public:
    CompSegVolHandler() = default;
    ~CompSegVolHandler() {
        m_volume = nullptr;
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

    static void loadSegmentationVolumeFile(std::string path, std::shared_ptr<Volume<uint32_t>> &volume,
                                           const std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> &label_remapping = nullptr,
                                           uint32_t cpu_threads = std::thread::hardware_concurrency()) {
        if (path.ends_with(".vraw") || path.ends_with(".raw")) {
            if (path.ends_with(".raw"))
                Logger(Warn) << "trying to open .raw file " << path << " as Volcanite raw (.vraw).";
            volume = Volume<uint32_t>::load_volcanite_raw(path);
        } else if (path.ends_with(".hdf5") || path.ends_with(".h5"))
            volume = Volume<uint32_t>::load_hdf5(path);
        else if (path.ends_with(".vti"))
            volume = Volume<uint32_t>::load_vti(path);
        else if (path.ends_with(".nrrd") || path.ends_with(".nhdr"))
            volume = Volume<uint32_t>::load_nrrd(path);
        else if (path.starts_with(CSGV_SYNTH_PREFIX_STR)) {
            volume = createDummySegmentationVolume(path);
        } else {
            const std::string _msg = "Segmentation volume filetype of " + path + " not supported!";
            throw std::runtime_error(_msg.c_str());
        }

#ifdef RELABEL_IDS_FROM_CSV_SUFFIX
        std::unordered_map<uint32_t, uint32_t> id_types;
        if (relabelVoxelsFromCSV(path + RELABEL_IDS_FROM_CSV_SUFFIX, id_types)) {
            Logger(Info, true) << "  CSV label remapping from " << path << RELABEL_IDS_FROM_CSV_SUFFIX;
            const size_t volume_size = volume->size();
            uint32_t *data = reinterpret_cast<uint32_t *>(volume->getRawData());
            static constexpr uint32_t NON_EXISTING_LABEL = 0u;
#pragma omp parallel for default(none) shared(data, id_types, volume_size)
            for (int i = 0; i < volume_size; i++) {
                if (id_types.find(data[i]) != id_types.end())
                    data[i] = id_types[data[i]];
                else
                    data[i] = NON_EXISTING_LABEL;
            }
            Logger(Info) << "  CSV label remapping from " << path << RELABEL_IDS_FROM_CSV_SUFFIX << " finished.";
        }
#endif

        // Remap all voxels to other labels. This usually happens because we computed a mapping in the attribute
        // database so that voxels are numbered in Z-order.
        if (label_remapping) {
            MiniTimer t;
            size_t voxel_count = volume->dim_x * volume->dim_y * volume->dim_z;
            auto voxels = volume->data().data();
#pragma omp parallel for num_threads(cpu_threads) default(none) shared(voxels, voxel_count, label_remapping)
            for (size_t i = 0; i < voxel_count; i++) {
                if (!label_remapping->contains(voxels[i]))
                    throw std::runtime_error("label remapping does not contain voxel label " + std::to_string(voxels[i]));
                voxels[i] = label_remapping->at(voxels[i]);
            }
            Logger(Debug) << "Attribute data base label remapping finished in " << t.elapsed() << " seconds.";
        }
    }

    static void decompressCompressedSegmentationVolume(std::shared_ptr<const CompressedSegmentationVolume> csgv, const std::string &output_path, glm::uvec3 chunk_size = glm::uvec3(1024u, 1024u, 1024u)) {
        const auto brick_size = csgv->getBrickSize();

        // if chunk_size is 0 in any dimension, disable chunking in this dimension
        // (set chunk_size to next multiple of brick_size after the volume dimension)
        for (int d = 0; d < 3; d++) {
            if (chunk_size[d] == 0u)
                chunk_size[d] = ((csgv->getVolumeDim()[d] + brick_size - 1u) / brick_size) * brick_size;
        }

        if (chunk_size.x % brick_size != 0 && chunk_size.y % brick_size != 0 && chunk_size.z % brick_size != 0)
            throw std::runtime_error("chunk size has to be a multiple of the brick size");

        const auto volume_dim = csgv->getVolumeDim();
        const auto number_of_bricks_per_chunk = chunk_size / brick_size;
        const auto combined_number_of_bricks_per_chunk = number_of_bricks_per_chunk.x * number_of_bricks_per_chunk.y * number_of_bricks_per_chunk.z;
        const auto number_of_output_chunks = glm::uvec3(glm::ceil(volume_dim.x / static_cast<float>(chunk_size.x)), glm::ceil(volume_dim.y / static_cast<float>(chunk_size.y)), glm::ceil(volume_dim.z / static_cast<float>(chunk_size.z)));
        const auto combined_number_of_output_chunks = number_of_output_chunks.x * number_of_output_chunks.y * number_of_output_chunks.z;

        // construct output_path template
        const std::string file_extension = output_path.substr(output_path.find_last_of('.'), output_path.length());
        const std::string chunk_output_path_template = output_path.substr(0, output_path.length() - 5) + "_x{}y{}z{}" + file_extension;

        // create directory of output file if it does not exist
        std::filesystem::create_directories(std::filesystem::path(chunk_output_path_template).remove_filename());

        const unsigned int cpu_threads = glm::min(std::thread::hardware_concurrency(), volume_dim.z);

        MiniTimer t;

        // allocate reusable memory for the output chunk once (with max. possible size)
        Volume<uint32_t> output_chunk{static_cast<float>(chunk_size.x), static_cast<float>(chunk_size.y), static_cast<float>(chunk_size.z),
                                            chunk_size.x, chunk_size.y, chunk_size.z, vk::Format::eUndefined, chunk_size.x * chunk_size.y * chunk_size.z};
        // cpu_threads many threads decode bricks in parallel (voxels in z-order) into tmp arrays
        std::vector<uint32_t> tmp_bricks[cpu_threads];
        for (int i = 0; i < cpu_threads; i++) {
            tmp_bricks[i].resize(brick_size * brick_size * brick_size);
        }

        // iterate over output chunks into which the volume is divided
        for (size_t chunk_idx = 0; chunk_idx < combined_number_of_output_chunks; chunk_idx++) {
            glm::uvec3 chunk_start_pos = brick_idx2pos(chunk_idx, number_of_output_chunks) * chunk_size; // chunk start position
                                                                                                         // const auto brick_idx_start = chunk_start_pos / brick_size; // brick_idx of first brick in chunk

            Logger(Info, true) << "decompressing volume " << output_path << " (chunk " << chunk_idx << "/" << combined_number_of_output_chunks << ") " << (static_cast<uint32_t>(t.elapsed()) / 60u) << "m" << (static_cast<uint32_t>(t.elapsed()) % 60u) << "s";

            // border chunks might be smaller than the full chunk size
            const glm::uvec3 output_chunk_size = glm::min(chunk_start_pos + chunk_size, volume_dim) - chunk_start_pos;
            // this does not resize the payload array of the output_chunk Volume so it would always fit a full chunk_size chunk
            output_chunk.dim_x = output_chunk_size.x;
            output_chunk.dim_y = output_chunk_size.y;
            output_chunk.dim_z = output_chunk_size.z;

            // decompress bricks from chunk in parallel
#pragma omp parallel for num_threads(cpu_threads) default(shared)
            for (size_t brick_count = 0; brick_count < combined_number_of_bricks_per_chunk; brick_count++) {
                // calculate the bricks position inside the resulting chunk
                const auto brick_global_thread_pos = brick_idx2pos(brick_count, number_of_bricks_per_chunk) * brick_size + chunk_start_pos; // start pos of brick in volume
                // const auto brick_global_thread_index = brick_pos2idx(brick_global_thread_pos / brick_size, csgv->getBrickCount()); // serialized idx of brick
                const auto brick_global_thread_id = brick_global_thread_pos / brick_size; // id of brick
                const auto brick_pos_in_chunk = brick_global_thread_pos % chunk_size;     // brick pos inside its resulting output chunk

                if (glm::any(glm::greaterThanEqual(brick_global_thread_id, csgv->getBrickCount())))
                    // outside of volume
                    continue;

                auto& tmp_brick = tmp_bricks[omp_get_thread_num()];
                csgv->decompressBrickTo(tmp_brick.data(), brick_global_thread_id, static_cast<int>(csgv->getLodCountPerBrick() - 1u));
                // fill output array with decoded brick entries
                for (uint32_t j = 0; j < brick_size * brick_size * brick_size; j++) {
                    if (glm::uvec3 pos_in_brick = enumBrickPos(j); glm::all(glm::lessThan(pos_in_brick, {brick_size, brick_size, brick_size}))) {
                        const auto pos_in_chunk = pos_in_brick + brick_pos_in_chunk;

                        // do not write voxels outside of chunk
                        if(glm::any(greaterThanEqual(pos_in_chunk, output_chunk_size)))
                            continue;

                        output_chunk.data()[brick_pos2idx(pos_in_chunk, output_chunk_size)] = tmp_brick[j];
                    }
                }
            }

            if (!write_output_chunk(chunk_output_path_template, chunk_start_pos / chunk_size, output_chunk)) {
                Logger(Error) << "could not write volume file(s) to " << output_path;
                return;
            }
        }

        Logger(Info) << "decompressed volume to " << output_path << " (" << combined_number_of_output_chunks << " chunk(s) in " << t.elapsed() << " seconds)";
    }

  private:
    std::string m_last_volume_path = {};
    glm::ivec3 m_volume_dim = glm::ivec3(0);
    std::shared_ptr<Volume<uint32_t>> m_volume = nullptr;
    void loadSegmentationVolumeFileCached(const std::string &path,
                                          const std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> &label_remapping = nullptr,
                                          uint32_t cpu_threads = std::thread::hardware_concurrency()) {
        // only load the volume file if it differs from the previously loaded volume
        if (path != m_last_volume_path || !m_volume) {
            loadSegmentationVolumeFile(path, m_volume, label_remapping, cpu_threads);
            m_volume_dim = glm::ivec3(m_volume->dim_x, m_volume->dim_y, m_volume->dim_z);
            m_last_volume_path = path;
        }
    }

    static bool write_output_chunk(const std::string &chunk_output_path_template, const glm::uvec3 chunk_id, const Volume<uint32_t> &decompressed_chunk) {
        // create volume without allocating payload

        const std::string chunk_output_path = formatChunkPath(chunk_output_path_template, chunk_id.x, chunk_id.y, chunk_id.z);
        if (std::filesystem::exists(chunk_output_path))
            std::filesystem::remove(chunk_output_path);

        return decompressed_chunk.write(chunk_output_path);
    }

  public:
    struct CSGVCompressionConfig {
        uint32_t brick_dim = 32;
        EncodingMode encoding_mode = DOUBLE_TABLE_RANS_ENC;
        uint32_t op_mask = OP_ALL;
        bool random_access = false;
        std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> label_remapping = nullptr;
        uint32_t cpu_threads = 0u;
        bool use_detail_separation = false;
        bool force_recompute = false;
        bool chunked_input_data = false;
        glm::uvec3 max_file_index = glm::uvec3(0u);
        uint32_t freq_subsampling = 8u;
        bool run_tests = false;
        bool export_stats_per_chunk = false;
        bool verbose = true;
    };

    std::shared_ptr<CompressedSegmentationVolume> createCompressedSegmentationVolume(const std::string &volume_input_path,
                                                                                     const std::string &csgv_path, const CSGVCompressionConfig &cfg) {
        uint32_t cpu_threads = cfg.cpu_threads;
        if (cpu_threads == 0u)
            cpu_threads = std::thread::hardware_concurrency();

        if (cfg.use_detail_separation && cfg.encoding_mode != DOUBLE_TABLE_RANS_ENC)
            throw std::runtime_error("Detail separation can only be used in combination with double table rANS.");
        if (cfg.freq_subsampling == 0u)
            throw std::runtime_error("Frequency subsampling must be at least 1 (= no subsampling).");
        if (cfg.use_detail_separation)
            Logger(Warn) << "Using detail separation is not recommended at compression stage and may be removed later.";
        if (cfg.random_access && cfg.encoding_mode != NIBBLE_ENC && cfg.encoding_mode != WAVELET_MATRIX_ENC && cfg.encoding_mode != HUFFMAN_WM_ENC)
            throw std::runtime_error("Random access can only be used in combination with wavelet matrix or nibble encoding.");

        const bool create_log_file = false;
        const bool create_operation_freq_file = cfg.chunked_input_data;
        double total_freq_prepass_seconds = 0.f;
        double total_encoding_seconds = 0.f;

        MiniTimer total_encoding_import_export_timer;

        // check output path for the complete volume
        if (!csgv_path.ends_with(".csgv")) {
            throw std::runtime_error("Output file must end with .csgv!");
        }

        // Compressing a chunked file can take a long time. We export all independently compressed chunks first, given
        // this file name template (creates a path like my/path/tmp_x{}_y{}_z{}_bs64_rANS2.csgv for example):
        std::string chunk_output_path_template = csgv_path.substr(0, csgv_path.length() - 5) + "_x{}_y{}_z{}.csgv";
        // we never separate the detail level in single chunk files.
        chunk_output_path_template = CompressedSegmentationVolume::getCSGVFileName(chunk_output_path_template, cfg.brick_dim, cfg.encoding_mode, false);

        if (cfg.verbose) {
            std::string op_mask_str = OperationMask_STR(cfg.op_mask);
            Logger(Info) << "Compressing " << volume_input_path
                         << (cfg.chunked_input_data ? " with chunk indices " + str(cfg.max_file_index) : "")
                         << " to " << csgv_path << " [b=" << cfg.brick_dim << ", e=" << EncodingMode_STR(cfg.encoding_mode)
                         << ", op=" << op_mask_str
                         << (cfg.random_access ? ", p" : "") << "]"
                         << (cfg.use_detail_separation ? " with lod separation" : "");
        }

        std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<volcanite::CompressedSegmentationVolume>();
        csgv->setCPUThreadCount(cpu_threads);
        // check if we can load a precomputed compressed segmentation volume
        if (!cfg.force_recompute && csgv->importFromFile(csgv_path, false)) {
            if (cfg.run_tests) {
                if (!cfg.chunked_input_data) {
                    loadSegmentationVolumeFileCached(volume_input_path, cfg.label_remapping, cpu_threads);
                    Logger(Info) << volume_input_path + " loaded with dim " << str(m_volume_dim);
                    if (!csgv->test(m_volume->data(), m_volume_dim)) {
                        return nullptr;
                    }
                } else {
                    Logger(Warn)
                        << "Testing not supported for pre-computed chunked data sets. Use force_recompute=true to do a full compression with a test per chunk.";
                }
            }
            Logger(Info) << "Imported previously compressed file " << csgv_path << ". Skipping compression.";
            return csgv;
        }

        // if we use rANS, we need to get a global frequency table shared over all chunks
        std::vector<size_t> code_frequencies(16, 0u);
        std::vector<size_t> detail_code_frequencies(16, 0u);
        // TODO: Generalize variable bit-length encoding frequency table computation
        if (cfg.encoding_mode == SINGLE_TABLE_RANS_ENC || cfg.encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            // We may have a precomputed frequency table.
            // As operation frequencies do not change between rANS in single table or no rANS mode, we could use the same filename to store precomputed freq. tables in both cases.
            std::string freq_path = CompressedSegmentationVolume::getCSGVFileName(csgv_path, cfg.brick_dim, cfg.encoding_mode, false, ".cfrq");
            if (!cfg.force_recompute && std::filesystem::exists(freq_path)) {
                Logger(Debug) << "using operation frequencies from file " << freq_path;
                std::ifstream freq_file(freq_path, std::ios_base::in | std::ios::binary);
                if (!freq_file.is_open()) {
                    Logger(Error) << "unable to open file " << freq_path << ". Aborting.";
                    return nullptr;
                }
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                for (int i = 0; i < 16; i++)
                    freq_file.read(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                freq_file.close();
            } else {
                Logger(Debug) << "operation frequency prepass:";
                // note: this is a hardcoded frequency subsampling (1/8th of all chunks) on a chunk level. Ccompression
                // time is dominated by file i/o and reading fewer chunks makes everything much faster.
                const int chunk_skip = ((cfg.max_file_index.z + cfg.max_file_index.z + cfg.max_file_index.z) > 4 && cfg.freq_subsampling > 1) ? 2 : 1;
                for (int z = 0; z <= cfg.max_file_index.z; z += chunk_skip) {
                    for (int y = 0; y <= cfg.max_file_index.y; y += chunk_skip) {
                        for (int x = 0; x <= cfg.max_file_index.x; x += chunk_skip) {
                            // create new file path for the compressed version of this single chunk
                            std::string chunk_input_path = cfg.chunked_input_data ? formatChunkPath(volume_input_path, x, y, z) : volume_input_path;

                            loadSegmentationVolumeFileCached(chunk_input_path, cfg.label_remapping, cpu_threads);

                            size_t tmp_code_frequencies[32];
                            csgv->setLabel(std::filesystem::path(chunk_input_path).stem().string());
                            csgv->setCompressionOptions({.brick_size = cfg.brick_dim, .encoding_mode = NIBBLE_ENC, .op_mask = cfg.op_mask, .random_access = cfg.random_access});
                            csgv->compressForFrequencyTable(m_volume->data(), m_volume_dim, tmp_code_frequencies, cfg.freq_subsampling, cfg.encoding_mode == DOUBLE_TABLE_RANS_ENC, false);
                            for (int i = 0; i < 16; i++) {
                                code_frequencies[i] += tmp_code_frequencies[i];
                                detail_code_frequencies[i] += tmp_code_frequencies[i + 16];
                            }
                            total_freq_prepass_seconds += csgv->getLastTotalFreqPrepassSeconds();
                        }
                    }
                }

                // Write some general info about the chunk to a file (as of now, only the operation frequencies)
                if (create_operation_freq_file) {
                    if (std::filesystem::exists(freq_path))
                        Logger(Warn) << "Overwriting existing file " << freq_path;
                    else if (!exists(std::filesystem::path(freq_path).parent_path()))
                        create_directory(std::filesystem::path(freq_path).parent_path());
                    std::ofstream freq_file(freq_path, std::ios_base::out | std::ios::binary);
                    if (freq_file.is_open()) {
                        for (int i = 0; i < 16; i++)
                            freq_file.write(reinterpret_cast<char *>(&code_frequencies[i]), sizeof(size_t));
                        for (int i = 0; i < 16; i++)
                            freq_file.write(reinterpret_cast<char *>(&detail_code_frequencies[i]), sizeof(size_t));
                        freq_file.close();
                    } else {
                        Logger(Warn) << "Unable to export operation frequencies to " << freq_path << ".";
                    }
                }
            }

            if (cfg.verbose) {
                Logger(Debug) << "frequencies: " << arrayToString(code_frequencies.data(), code_frequencies.size())
                              << " | detail frequencies: " << arrayToString(detail_code_frequencies.data(), detail_code_frequencies.size());
            }
            Logger(Debug) << "";
            Logger(Debug) << "";
            Logger(Debug) << "Compression pass:";
        }

        // now we encode every chunk on its own and store the result on the hard drive
        for (int z = 0; z <= cfg.max_file_index.z; z++) {
            for (int y = 0; y <= cfg.max_file_index.y; y++) {
                for (int x = 0; x <= cfg.max_file_index.x; x++) {

                    // create file input and output paths for this single chunk
                    std::string chunk_input_path = cfg.chunked_input_data ? formatChunkPath(volume_input_path, x, y, z) : volume_input_path;
                    std::string chunk_output_path = cfg.chunked_input_data ? formatChunkPath(chunk_output_path_template, x, y, z) : csgv_path;

                    bool recompute = cfg.force_recompute || (cfg.max_file_index.x + cfg.max_file_index.y + cfg.max_file_index.z == 0u) // if this is just one chunk, we also have to recompute at this point
                                     || !csgv->importFromFile(chunk_output_path, false);
                    if (recompute) {
                        loadSegmentationVolumeFileCached(chunk_input_path, cfg.label_remapping, cpu_threads);
                        if (cfg.verbose) {
                            Logger(Info) << " " << chunk_input_path + " loaded with dim " << str(m_volume_dim);
                            Logger(Info) << "Running Encoding  --------------------------------------------";
                        }

                        // perform the actual compression
                        csgv->clear();
                        csgv->setLabel(chunk_input_path);
                        csgv->setCompressionOptions({.brick_size = cfg.brick_dim, .encoding_mode = cfg.encoding_mode, .op_mask = cfg.op_mask, .random_access = cfg.random_access, .code_frequencies = code_frequencies.data(), .detail_code_frequencies = detail_code_frequencies.data()});
                        csgv->compress(m_volume->data(), m_volume_dim, cfg.verbose);
                        total_encoding_seconds += csgv->getLastTotalEncodingSeconds();
                        if (std::filesystem::exists(chunk_output_path)) {
                            Logger(Warn) << "overwriting file " << chunk_output_path;
                            std::filesystem::remove(chunk_output_path);
                        }

                        if (cfg.run_tests && !csgv->test(m_volume->data(), m_volume_dim)) {
                            return nullptr;
                        }

                        csgv->exportToFile(chunk_output_path);
                    } else {
                        if (cfg.verbose) {
                            Logger(Info) << " reusing existing csgv file " << chunk_output_path << " " << csgv->getEncodingInfoString();
                        } else {
                            Logger(Info) << " reusing existing csgv file " << chunk_output_path;
                        }

                        if (cfg.run_tests) {
                            if (!m_volume) {
                                loadSegmentationVolumeFileCached(chunk_input_path, cfg.label_remapping, cpu_threads);
                                Logger(Info) << chunk_input_path + " loaded with dim " << str(m_volume_dim);
                            }
                            if (!csgv->test(m_volume->data(), m_volume_dim)) {
                                return nullptr;
                            }
                        }
                    }

                    if (cfg.export_stats_per_chunk) {
                        std::string stats_path = csgv_path.substr(0, csgv_path.find_last_of('.')) + "_brickstats.csv";
                        Logger(Debug, true) << "export brick statistics to " << stats_path;
                        csv_export(csgv->gatherBrickStatistics(), stats_path);
                        Logger(Debug) << "export brick statistics to " << stats_path + " done";
                    }
                }
            }
        }

        // if we have multiple chunks, we have to merge them
        Logger(Info) << "Total raw compression time: " << std::setprecision(3) << total_freq_prepass_seconds << " + "
                     << total_encoding_seconds << " = " << (total_freq_prepass_seconds + total_encoding_seconds) << "s, "
                     << "including file IO: " << total_encoding_import_export_timer.elapsed() << "s.";
        if (cfg.chunked_input_data && glm::any(glm::greaterThan(cfg.max_file_index, glm::uvec3(0)))) {
            CSGVChunkMerger merger;
            csgv = merger.mergeCompressedSegmentationVolumeChunksFromFiles(csgv_path, chunk_output_path_template, cfg.max_file_index);
            if (!csgv)
                return nullptr;
            csgv->setCPUThreadCount(cpu_threads);
            csgv->m_last_total_freq_prepass_seconds = static_cast<float>(total_freq_prepass_seconds);
            csgv->m_last_total_encoding_seconds = static_cast<float>(total_encoding_seconds);
        }

        // create a log file
        if (create_log_file) {
            std::ofstream file(csgv->getCSGVFileName(csgv_path) + ".log", std::ios_base::out);
            if (!file.is_open()) {
                Logger(Error) << "Unable to open file " << csgv_path << ".log. Skipping.";
            } else {
                file << MiniTimer::getCurrentDateTime() << std::endl;
                file << "Compression time [s] excluding file import and export:" << std::endl;
                file << "  Frequency prepass: " << total_freq_prepass_seconds << "s" << std::endl;
                file << "   Compression pass: " << total_encoding_seconds << "s" << std::endl;
                file << "  Total compression: " << (total_freq_prepass_seconds + total_encoding_seconds) << std::endl;
                file << "" << std::endl;
                file << "Compressed volume information:" << std::endl;
                file << "  " << csgv->getEncodingInfoString() << std::endl;
                file.close();
            }
        }

        // remove all temporary files created during the compression
        if (cfg.chunked_input_data && glm::any(glm::greaterThan(cfg.max_file_index, glm::uvec3(0)))) {
            for (int z = 0; z <= cfg.max_file_index.z; z++) {
                for (int y = 0; y <= cfg.max_file_index.y; y++) {
                    for (int x = 0; x <= cfg.max_file_index.x; x++) {
                        std::string chunk_output_path = formatChunkPath(chunk_output_path_template, x, y, z);
                        if (std::filesystem::exists(chunk_output_path))
                            std::filesystem::remove(chunk_output_path);
                    }
                }
            }
            std::string s;
            s = csgv_path.substr(0, csgv_path.length() - 5) + "_brickstarts.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = csgv_path.substr(0, csgv_path.length() - 5) + "_detailstarts.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = csgv_path.substr(0, csgv_path.length() - 5) + "_encoding.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = csgv_path.substr(0, csgv_path.length() - 5) + "_detail.tmp";
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
            s = CompressedSegmentationVolume::getCSGVFileName(csgv_path, cfg.brick_dim, cfg.encoding_mode, false, ".cfrq");
            if (std::filesystem::exists(s))
                std::filesystem::remove(s);
        }

        if (cfg.use_detail_separation) {
            csgv->separateDetail();
        }

        Logger(Info) << "Total info: " << csgv->getEncodingInfoString();
        return csgv;
    }
};

} // namespace volcanite
