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
#ifdef HEADLESS
    #include "vvv/headless_entrypoint.hpp"
#else
    #include "vvvwindow/App.hpp"
    #include "vvvwindow/entrypoint.hpp"
#endif

#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"
#include "volcanite/compression/CSGVDatabase.hpp"

#include "csgv_constants.incl"

using namespace volcanite;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_NOT_SUPPORTED = 2;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;
constexpr int RET_IO_ERROR = 5;



int export_texture(Texture* tex, const std::string& export_file_path) {
    try {
        Logger(INFO) << "Exporting render output to " << export_file_path;
        tex->writeFile(export_file_path);
    }
    catch(const std::runtime_error& e) {
        Logger(ERROR) << "Render export error: " << e.what();
        return RET_IO_ERROR;
    }
    return 0;
}

std::string dtos(double v, int decimal_places=3) {
    v = std::round(v * pow(10., decimal_places)) / pow(10., decimal_places);
    std::string s = std::to_string(v);
    auto decpos = s.rfind('.');
    int skipped_chars = 0;
    if (decpos != std::string::npos) {
        auto lpos = s.end() - 1;
        while (*(lpos - skipped_chars) == '0')
            skipped_chars++;
        if (*(lpos - skipped_chars) == '.')
            skipped_chars++;
    }
    return s.substr(0, s.size() - skipped_chars);
}

std::string format_evaluation_string(std::string format_string, const std::string& eval_name, int argc, char *argv[],
                                     CSGVCompressionEvaluationResults comp_res,
                                     CSGVDecompressionEvaluationResults decomp_res,
                                     CSGVRenderEvaluationResults render_res) {
    // obtain time stamp
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::stringstream time_stamp_ss;
    time_stamp_ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    // obtain args string
    std::stringstream args_ss;
    for (int i = 0; i < argc; i++) {
        args_ss << argv[i];
        if (i < argc - 1)
            args_ss << " "; // if arguments should be comma separated, this would be ","
    }

    // the list of replacement specifiers and the replacement values:
    std::vector<std::pair<std::string, std::string>> replace_str = {
            {"name", eval_name.empty() ? ("eval-" + time_stamp_ss.str()) : eval_name},
            {"time", time_stamp_ss.str()},
            {"args", args_ss.str()},
            // compression
            {"cr", dtos(comp_res.compression_rate * 100.)},
            {"comp_s", dtos(comp_res.compression_total_seconds)},
            {"comp_mainpass_s", dtos(comp_res.compression_mainpass_seconds)},
            {"comp_prepass_s", dtos(comp_res.compression_prepass_seconds)},
            {"comp_gb_per_s", dtos(comp_res.compression_GB_per_s)},
            {"csgv_gb", dtos(comp_res.csgv_bytes * BYTE_TO_GB)},
            {"orig_gb", dtos(comp_res.original_volume_bytes * BYTE_TO_GB)},
            {"volume_dim", std::to_string(comp_res.volume_dim.x) + "x" + std::to_string(comp_res.volume_dim.y) + "x"
                            + std::to_string(comp_res.volume_dim.z)},
            // decompression
            {"decomp_cpu_gb_per_s", dtos(decomp_res.cpu_GB_per_s)},
            {"decomp_gpu_gb_per_s", dtos(decomp_res.gpu_GB_per_s)},
            // rendering
            {"frame_min_ms", dtos(render_res.frame_min_ms)},
            {"frame_avg_ms", dtos(render_res.frame_avg_ms)},
            {"frame_sdv_ms", dtos(render_res.frame_sdv_ms)},
            {"frame_med_ms", dtos(render_res.frame_med_ms)},
            {"frame_max_ms", dtos(render_res.frame_max_ms)},
            {"render_total_ms", dtos(render_res.total_ms)},
            {"mem_framebuffer_mb", dtos(render_res.mem_framebuffers_bytes * BYTE_TO_MB)},
            {"mem_uniformbuffer_mb", dtos(render_res.mem_ubos_bytes * BYTE_TO_MB)},
            {"mem_materials_mb", dtos(render_res.mem_materials_bytes * BYTE_TO_MB)},
            {"mem_encoding_mb", dtos(render_res.mem_encoding_bytes * BYTE_TO_MB)},
            {"mem_cache_mb", dtos(render_res.mem_cache_bytes * BYTE_TO_MB)},
            {"mem_emptyspace_mb", dtos(render_res.mem_empty_space_bytes * BYTE_TO_MB)},
            {"mem_total_mb", dtos(render_res.mem_total_bytes * BYTE_TO_MB)},
            {"render_frames", dtos(render_res.accumulated_frames)},
    };
    for (int i = 0; i < 10; i++) {
        replace_str.emplace_back("frame_ms_0" + std::to_string(i), dtos(render_res.frame_ms[i]));
        if (i < 6)
            replace_str.emplace_back("frame_ms_1" + std::to_string(i), dtos(render_res.frame_ms[10 + i]));
    }

    // replace all occurrences of all specifiers
    bool replaced;
    do {
        replaced = false;
        for (const auto& r: replace_str) {
            // TODO: support a std::vformat style replacement to control decimal points etc.
            auto replace_key = "%" + r.first;
            auto pos = format_string.find(replace_key);
            if (pos != std::string::npos) {
                format_string.replace(pos, replace_key.size(), r.second);
                replaced = true;
            }
        }
    } while (replaced);
    return format_string;
}

int write_eval_logfile(const std::string& eval_logfile, const std::string& eval_name, int argc, char *argv[],
                       CSGVCompressionEvaluationResults comp_res,
                       CSGVDecompressionEvaluationResults decomp_res,
                       CSGVRenderEvaluationResults render_res) {
    bool logfile_exists = std::filesystem::exists(eval_logfile);
    std::string format_string;
    std::string header_string;
    if (logfile_exists) {
        std::ifstream file = std::ifstream(eval_logfile);
        if (file.is_open()) {
            std::string line;
            std::getline(file, line);
            while (line.starts_with("#fmt:")) {
                line = line.substr(5);
                format_string += (line + "\n");
            }
            if (format_string.ends_with('\n'))
                format_string.pop_back(); // remove trailing '\n'
            file.close();
        } else {
            Logger(ERROR) << "Could not open pre-existing evaluation log file " << eval_logfile;
            return RET_IO_ERROR;
        }
    }

    if (format_string.empty()) {
        // TODO: automatically create the default format and header from the replacement specifier vector
        // the default header and format string:
        header_string = "# to remove the comment lines from the file use: sed -i '/#/d' ./" + eval_logfile + "\n"
                        "name,volume_dim,orig_GB,csgv_GB,CR_%,comp_s,comp_GB_per_s,"
                        "decomp_cpu_GB_per_s,decomp_gpu_GB_per_s,frame_min_ms,frame_avg_ms,frame_sdv_ms,frame_med_ms,frame_max_ms,"
                        "render_total_ms,mem_framebuffer_MB,mem_uniformbuffer_MB,mem_materials_MB,mem_encoding_MB,"
                        "mem_cache_MB,mem_emptyspace_MB,mem_total_MB,render_frames,"
                        "frame_ms_00,frame_ms_01,frame_ms_02,frame_ms_03,frame_ms_04,frame_ms_05,frame_ms_06,"
                        "frame_ms_07,frame_ms_08,frame_ms_09,frame_ms_10,frame_ms_11,frame_ms_12,frame_ms_13,"
                        "frame_ms_14,frame_ms_15";
        format_string = "#%time [%args] result:\n"
                        "%name,%volume_dim,%orig_GB,%csgv_GB,%cr,%comp_s,%comp_GB_per_s,%"
                        "decomp_cpu_GB_per_s,%decomp_gpu_GB_per_s,%frame_min_ms,%frame_avg_ms,%frame_sdv_ms,%frame_med_ms,%frame_max_ms,%"
                        "render_total_ms,%mem_framebuffer_MB,%mem_uniformbuffer_MB,%mem_materials_MB,%mem_encoding_MB,%"
                        "mem_cache_MB,%mem_emptyspace_MB,%mem_total_MB,%render_frames,"
                        "%frame_ms_00,%frame_ms_01,%frame_ms_02,%frame_ms_03,%frame_ms_04,%frame_ms_05,%frame_ms_06,"
                        "%frame_ms_07,%frame_ms_08,%frame_ms_09,%frame_ms_10,%frame_ms_11,%frame_ms_12,%frame_ms_13,"
                        "%frame_ms_14,%frame_ms_15";
    }
    std::ofstream output_file = std::ofstream(eval_logfile, std::ios_base::app);
    if (!output_file.is_open()) {
        Logger(ERROR) << "Could not open evaluation log file " << eval_logfile;
        return RET_IO_ERROR;
    }

    /* All first lines starting with #fmt: are concatenated into the format string
     *   #fmt:#header,time
     *   #fmt:%name,%time
     * becomes:
     *   #header,time\n%name,%time
     * which will be written out as:
     *   #header,time
     *   my_name,XX-XX-XXTXX:XX:XX
     *
     * To remove all comment lines from a file:
     * sed -i '/#/d' ./[eval_logfile]
     */

    // write out the current format string if this is a new file
    if (!logfile_exists) {
        // start all newlines in the format string with "#fmt:"
        std::string out_fmt_string = format_string;
        int pos = 0;
        while (true) {
            pos = out_fmt_string.find('\n', pos);
            if (pos == std::string::npos)
                break;
            out_fmt_string.replace(pos, 1, "\n#fmt:");
            pos += 6;
        }
        output_file << "#fmt:" << out_fmt_string << std::endl;
        // add the header string that is not part of the format string
        if (!header_string.empty())
            output_file << header_string << std::endl;
    }
    // replace all replacement specifiers in the format string
    output_file << format_evaluation_string(format_string, eval_name, argc, argv, comp_res, decomp_res, render_res)
                << std::endl;
    output_file.close();
    return RET_SUCCESS;
}

int tryImportRenderConfig(VolcaniteArgs& args, std::shared_ptr<CompressedSegmentationVolumeRenderer> renderer) {
    // set the startup resolution
    //renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});
    // read optional config file
    if(!args.rendering_config_file.empty()) {
        if (!renderer->readParameterFile(args.rendering_config_file, VOLCANITE_VERSION))
            return RET_INVALID_ARG;
    }
    return 0;
}

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
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase = std::make_shared<volcanite::CSGVDatabase>();
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


        if(!args.label_remapping && !args.attribute_database.empty()) {
            Logger(ERROR) << "Attribute database can not be used without label remapping. Aborting.";
            return RET_INVALID_ARG;
        }

        // we open a precomputed csgv database for this volume if it exists or create it otherwise
        std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> label_remapping = nullptr;
        if(args.label_remapping) {
            std::string database_path = stripFileExtension(complete_csgv_path) + "_csgv.db3";
            MiniTimer t;
            Logger(INFO) << "Initializing attribute database " << database_path;
            csgvDatabase->importOrProcessChunkedVolume(args.input_file, database_path,
                                                       args.attribute_database, args.attribute_table,
                                                       args.attribute_label,
                                                       args.chunked, max_chunk_id);
            // obtain the label re-mapping from the database
            label_remapping = csgvDatabase->getLabelRemapping();
            if (args.verbose)
                Logger(INFO) << "  finished in " << t.elapsed() << " seconds";
        } else {
            csgvDatabase->createDummy();
        }

        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = static_cast<int>(args.brick_size),
                                                        .encoding_mode = args.encoding_mode,
                                                        .op_mask = args.operation_mask,
                                                        .random_access = args.random_access,
                                                        .label_remapping = label_remapping,
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

        // if no config file was specified, use a config file at the export or import location if it exists
        if (args.rendering_config_file.empty()) {
            std::string config_path = stripFileExtension(complete_csgv_path) + ".vcfg";
            if (std::filesystem::exists(config_path))
                args.rendering_config_file = config_path;
            else {
                config_path = stripFileExtension(args.input_file) + ".vcfg";
                if (std::filesystem::exists(config_path))
                    args.rendering_config_file = config_path;
            }
        }
    }
    // otherwise, we load a previously compressed volume
    else {
        compressedSegmentationVolume = std::make_shared<CompressedSegmentationVolume>();
        if(!compressedSegmentationVolume->importFromFile(args.input_file, args.verbose)) {
            Logger(ERROR) << "could not load Compressed Segmentation Volume. Aborting.";
            return RET_COMPR_ERROR;
        }


        // try to load a precomputed database
        std::string database_path = stripFileExtension(args.input_file) + "_csgv.db3";
        if(std::filesystem::exists(database_path)) {
            MiniTimer t;
            csgvDatabase->importFromSqlite(database_path);
            if (args.verbose)
                Logger(INFO) << "Imported attribute database " << database_path << " in " << t.elapsed() << " seconds";
        }
        else {
            csgvDatabase->createDummy();
            Logger(INFO) << "No attribute database " << database_path << " found. Using dummy database.";
        }

        if(args.verbose) {
            Logger(DEBUG) << compressedSegmentationVolume->getEncodingInfoString();
        }

        // if no config file was specified, use a previous config next to the volume input or .csgv file, if it exists
        if (args.rendering_config_file.empty()) {
            std::string config_path = stripFileExtension(args.input_file) + ".vcfg";
            if (std::filesystem::exists(config_path))
                args.rendering_config_file = config_path;
        }
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

    // we only need the rendering part for screenshots or the interactive app
    std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString();
    bool run_headless_pass = !args.screenshot_output_file.empty() || !args.video_output_fmt_file.empty()
                                || !args.eval_logfile.empty();
    if (!args.headless || run_headless_pass) {
        Logger(INFO) << "--------------------------------------------------- ";
        Logger(INFO) << "initializing Volcanite renderer";

        // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
        if(args.stream_lod && !compressedSegmentationVolume->isUsingSeparateDetail()) {
            Logger(DEBUG) << "separating detail level encoding for streaming";
            compressedSegmentationVolume->separateDetail();
            Logger(DEBUG) << compressedSegmentationVolume->getEncodingInfoString();
        }

        // if the attribute database is a dummy, we update the min/max attribute values for the volume labels
        if(csgvDatabase->isDummy())
            csgvDatabase->updateDummyMinMax(*compressedSegmentationVolume);

        const auto renderer = std::make_shared<volcanite::CompressedSegmentationVolumeRenderer>(!args.show_development_gui);
        renderer->setDecodingParameters({.cache_size_MB=args.cache_size_MB,
                                         .palettized_cache=args.cache_palettized,
                                         .decode_from_shared_memory=args.decode_from_shared_memory,
                                         .cache_mode=args.cache_mode,
                                         .empty_space_resolution=args.empty_space_resolution});
        renderer->setCompressedSegmentationVolume(compressedSegmentationVolume, csgvDatabase);
        renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});

        // if a screenshot file, video file, or evaluation log file path is given, run the headless mode first
        if (run_headless_pass) {

            // obtain a headless rendering engine
            auto renderEngine = HeadlessRendering::create("Volcanite", renderer, std::make_shared<DebugUtilsExt>());
            renderEngine->acquireResources();
            tryImportRenderConfig(args, renderer);
            // if frame count is specified in the rendering config, use that number.
            int accumulation_frames = renderer->getTargetAccumulationFrames();
            // if a recording file is given, play the full recording file instead (flagged as frame count 0)
            if (!args.record_in_file.empty())
                accumulation_frames = 0;
            else if (accumulation_frames == 0)
                accumulation_frames = 60;

            if (!args.eval_logfile.empty())
                renderer->startFrameTimeTracking();
            auto texture = renderEngine->renderFrames(accumulation_frames,
                                                      args.record_in_file,
                                                      args.video_output_fmt_file);
            if (!args.eval_logfile.empty())
                renderer->stopFrameTimeTracking({}); // stopFrameTimeTracking is already called by renderEngine

            // export final frame
            if (!args.screenshot_output_file.empty()
                && (texture == nullptr || export_texture(texture.get(), args.screenshot_output_file))) {
                Logger(ERROR) << "could not export final render frame to " << args.screenshot_output_file;
                return RET_RENDER_ERROR;
            }
            if (!args.eval_logfile.empty()) {
                CSGVCompressionEvaluationResults comp_res = compressedSegmentationVolume->getLastEvaluationResults();
                CSGVDecompressionEvaluationResults decomp_res; // TODO: decompression benchmark
                CSGVRenderEvaluationResults render_res = renderer->getLastEvaluationResults();

                if (write_eval_logfile(args.eval_logfile, args.eval_name, argc, argv,
                                       comp_res, decomp_res, render_res) == RET_SUCCESS) {
                    Logger(INFO) << "exported evaluation results to " << args.eval_logfile;
                } else {
                    Logger(WARN) << "could not export evaluation results to " << args.eval_logfile;
                    return RET_IO_ERROR;
                }
            }
            texture.reset();
            texture = nullptr;
            renderEngine->releaseResources();
        }

#ifndef HEADLESS
        // only start the application if we are not in headless mode
        if (!args.headless) {
            // export the state of the renderer next to the input or csgv volume when the app is closed
            if(!args.performCompression() || args.compress_export_file.empty())
                renderer->saveConfigOnShutdown(stripFileExtension(args.input_file) + ".vcfg");
            else
                renderer->saveConfigOnShutdown(stripFileExtension(args.compress_export_file) + ".vcfg");

            bool vsync = true;  // TODO: vsync should be a parameter of the CompressedSegmentationVolumeRenderer config
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());
            app->setStartupWindowSize({args.render_resolution[0], args.render_resolution[1]});
            app->setVSync(vsync);
            app->acquireResources();
            tryImportRenderConfig(args, renderer);
            return app->exec();
        }
#endif
    }

    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_main)
