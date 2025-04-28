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

  public:
    CompSegVolHandler() = default;

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

    struct CSGVCompressionConfig {
        int brick_dim = 32;
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

    static std::shared_ptr<CompressedSegmentationVolume> createCompressedSegmentationVolume(const std::string &volume_input_path,
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

        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        glm::ivec3 volume_dim(0);

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
                    loadSegmentationVolumeFile(volume_input_path, volume, cfg.label_remapping, cpu_threads);
                    volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                    Logger(Info) << volume_input_path + " loaded with dim " << str(volume_dim);
                    if (!csgv->test(volume->data(), volume_dim)) {
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

                            loadSegmentationVolumeFile(chunk_input_path, volume, cfg.label_remapping, cpu_threads);
                            volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);

                            size_t tmp_code_frequencies[32];
                            csgv->setLabel(std::filesystem::path(chunk_input_path).stem().string());
                            csgv->setCompressionOptions(cfg.brick_dim, NIBBLE_ENC, cfg.op_mask, cfg.random_access);
                            csgv->compressForFrequencyTable(volume->data(), volume_dim, tmp_code_frequencies, cfg.freq_subsampling, cfg.encoding_mode == DOUBLE_TABLE_RANS_ENC, false);
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
                        loadSegmentationVolumeFile(chunk_input_path, volume, cfg.label_remapping, cpu_threads);
                        volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                        if (cfg.verbose) {
                            Logger(Info) << " " << chunk_input_path + " loaded with dim " << str(volume_dim);
                            Logger(Info) << "Running Encoding  --------------------------------------------";
                        }

                        // perform the actual compression
                        csgv->clear();
                        csgv->setLabel(chunk_input_path);
                        csgv->setCompressionOptions64(cfg.brick_dim, cfg.encoding_mode, cfg.op_mask, cfg.random_access,
                                                      code_frequencies.data(), detail_code_frequencies.data());
                        csgv->compress(volume->data(), volume_dim, cfg.verbose);
                        total_encoding_seconds += csgv->getLastTotalEncodingSeconds();
                        if (std::filesystem::exists(chunk_output_path)) {
                            Logger(Warn) << "overwriting file " << chunk_output_path;
                            std::filesystem::remove(chunk_output_path);
                        }

                        if (cfg.run_tests && !csgv->test(volume->data(), volume_dim)) {
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
                            if (!volume) {
                                loadSegmentationVolumeFile(chunk_input_path, volume, cfg.label_remapping, cpu_threads);
                                volume_dim = glm::ivec3(volume->dim_x, volume->dim_y, volume->dim_z);
                                Logger(Info) << chunk_input_path + " loaded with dim " << str(volume_dim);
                            }
                            if (!csgv->test(volume->data(), volume_dim)) {
                                return nullptr;
                            }
                        }
                    }

                    if (cfg.export_stats_per_chunk) {
                        Logger(Debug, true) << "export brick statistics...";
                        std::string stats_path = csgv_path.substr(0, csgv_path.find_last_of('.')) + "_brickstats.csv";
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
