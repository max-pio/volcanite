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

#include <string>
#include "vvv/util/Logger.hpp"
#include "vvv/util/detect_debugger.hpp"
#include "vvv/core/HeadlessRendering.hpp"
#include "vvv/headless_entrypoint.hpp"


#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "vvv/volren/Volume.hpp"
#include "volcanite/benchmark/CSGVBenchmarkPass.hpp"

using namespace volcanite;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_NOT_SUPPORTED = 2;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;


int volcanite_main(int argc, char *argv[]) {
    // parse command line arguments
    VolcaniteArgs args;
    {
        auto _args = VolcaniteArgs::parseArguments(argc, argv);
        if(!_args.has_value()) {
            Logger(ERROR) << "Exiting because of invalid arguments. See volcanite --help for available commands.";
            return RET_INVALID_ARG;
        }
        args = _args.value();
    }

    if(!vvv::debuggerIsAttached() && !args.verbose)
        Logger::s_minLevel = INFO;

    std::shared_ptr<volcanite::CompressedSegmentationVolume> compressedSegmentationVolume;
    // if we have to compress the input file (.vti/.raw/.hdf5..) we do it here
    if(args.performCompression()) {
        glm::uvec3 max_chunk_id = glm::uvec3(args.chunk_files[0], args.chunk_files[1], args.chunk_files[2]);
        if(!args.verbose) {
            Logger(INFO) << "compressing segmentation volume " << args.input_file
                         << (args.chunked ? " with max. chunks " + str(max_chunk_id) : "");
        }

        std::string complete_csgv_path = {};
        bool use_temporary_output_file = args.compress_export_file.empty();
        // if no output file is specified, we try to export the .csgv file to the location of the input file
        if(use_temporary_output_file) {
            std::string potential_path = stripFileExtension(args.input_file) + ".csgv";
            // this only works if the input path is not a formatted chunked input path,
            if (!args.chunked && !std::filesystem::exists(potential_path)) {
                std::ofstream file;
                file.open(potential_path);
                if (file.is_open()) {
                    file.close();
                    std::filesystem::remove(potential_path);
                    complete_csgv_path = potential_path;
                    use_temporary_output_file = false;
                }
            }
        }
        else {
            complete_csgv_path = args.compress_export_file;
        }

        // otherwise, we just use a tmp file
        if(use_temporary_output_file) {
            create_directory(std::filesystem::temp_directory_path() / "volcanite");
            complete_csgv_path = (std::filesystem::temp_directory_path() / "volcanite" / "tmp.csgv").string();
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }

        if(!args.label_remapping || !args.attribute_database.empty()) {
            Logger(WARN) << "Ignoring label remapping and attribute database in CSGV benchmark.";
        }

        // we open a precomputed csgv database for this volume if it exists or create it otherwise
        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = static_cast<int>(args.brick_size),
                .rANS_mode = args.rANS_mode,
                .parallel_decoding = args.random_access,
                .label_remapping = nullptr,
                .cpu_threads = args.threads,
                .use_detail_separation = args.stream_lod,
                .force_recompute = !args.chunked,
                .chunked_input_data = args.chunked,
                .max_file_index = max_chunk_id,
                .freq_subsampling = args.freq_subsampling,
                .run_tests = args.run_tests,
                .export_stats_per_chunk = args.export_stats && args.chunked,
                .verbose = args.verbose};
        compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(args.input_file,
                                                                                             complete_csgv_path, cfg);

        if(use_temporary_output_file) {
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }
    }
        // otherwise, we load a previously compressed volume
    else {
        compressedSegmentationVolume = std::make_shared<CompressedSegmentationVolume>();
        if(!compressedSegmentationVolume->importFromFile(args.input_file, args.verbose)) {
            Logger(ERROR) << "could not load Compressed Segmentation Volume. Aborting.";
            return RET_COMPR_ERROR;
        }


        if(args.verbose) {
            Logger(DEBUG) << compressedSegmentationVolume->getEncodingInfoString();
        }

        // if a config file exists next to the .csgv file, we use it to initialize the renderer
        std::string config_path = stripFileExtension(args.input_file) + ".vcfg";
        if(std::filesystem::exists(config_path))
            args.rendering_config_file = config_path;
    }

    if (compressedSegmentationVolume == nullptr) {
        Logger(ERROR) << "could not create or load Compressed Segmentation Volume. Aborting.";
        return RET_COMPR_ERROR;
    }

    if(args.performDecompression()) {
        // TODO: add decompression
        Logger(ERROR) << "decompression not yet supported";
        return RET_NOT_SUPPORTED;
    }

    if(args.export_stats) {
        Logger(INFO, true) << "export brick statistics...";
        std::string stats_path = stripFileExtension(args.input_file) + "_brickstats.csv";
        csv_export(compressedSegmentationVolume->gatherBrickStatistics(), stats_path);
        Logger(INFO) << "export brick statistics to " << stats_path + " done";
    }

    // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
    if(args.stream_lod && !compressedSegmentationVolume->isUsingSeparateDetail()) {
        Logger(DEBUG) << "separating detail level encoding.";
        compressedSegmentationVolume->separateDetail();
        Logger(DEBUG) << compressedSegmentationVolume->getEncodingInfoString();
    }


    Logger(INFO) << "--------------------------------------------------- ";
    Logger(INFO) << "Starting CSGV GPU decompression benchmark";

    DefaultGpuContext ctx;
    ctx.enableDeviceExtension("VK_EXT_memory_budget");
    ctx.physicalDeviceFeaturesV12().setBufferDeviceAddress(true);
    ctx.createGpuContext();
    CSGVBenchmarkPass benchmark(&(*compressedSegmentationVolume), &ctx);

    std::shared_ptr<Awaitable> awaitable;
    awaitable = benchmark.execute();
    ctx.sync->hostWaitOnDevice({awaitable});

    benchmark.freeResources();
    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_main)
