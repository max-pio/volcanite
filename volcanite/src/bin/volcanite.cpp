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

#include "vvv/core/HeadlessRendering.hpp"
#include "vvv/util/Logger.hpp"
#ifdef HEADLESS
#include "vvv/headless_entrypoint.hpp"
#else
#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"
#endif

#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CSGVDatabase.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/eval/EvaluationLogExport.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "volcanite/util/args_and_csgv_provider.hpp"
#include "vvv/util/video_encoding.hpp"
#include "vvv/volren/Volume.hpp"

#include <string>

#include <fmt/core.h>

using namespace volcanite;

int export_texture(Texture *tex, const std::string &export_file_path) {
    try {
        Logger(Info) << "Exporting render output to " << export_file_path;
        tex->writeFile(export_file_path);
    } catch (const std::runtime_error &e) {
        Logger(Error) << "Render export error: " << e.what();
        return RET_IO_ERROR;
    }
    return 0;
}

int tryImportRenderConfigs(VolcaniteArgs &args, std::shared_ptr<CompressedSegmentationVolumeRenderer> renderer) {
    // set the startup resolution
    // renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});
    // the config arg is a list of vcfg files

    for (const auto &config : args.rendering_configs) {
        if (config.ends_with(".vcfg") || renderer->getParameterPreset(config) != nullptr) {
            if (!renderer->readParameterFile(config, VOLCANITE_VERSION))
                return RET_INVALID_ARG;
        } else {
            // construct a string stream from the config string which must be of the form:
            // [window_name] {parameter_label_1}: {parameter_values_1}
            long window_name_end = static_cast<long>(config.find(']'));
            long label_name_end = static_cast<long>(config.find(':'));
            if (!config.starts_with('[') || window_name_end == std::string::npos || label_name_end == std::string::npos || label_name_end <= window_name_end) {
                Logger(Warn) << "Invalid config '" << config << "'. Configs must be in the form [{window}] {label}: {values}";
                continue;
            }
            std::stringstream vcfg_stream;
            // first line is the window name: [{window}]\n
            vcfg_stream << config.substr(0, window_name_end + 1) << '\n';
            // folllowed by another line for the parameter: {label}: {values}
            std::string_view label_view(config.begin() + window_name_end + 1, config.begin() + label_name_end + 1); // end of config string
            label_view.remove_prefix(std::min(label_view.find_first_not_of(' '), label_view.size()));               // remove leading spaces
            auto sanitized_string = std::string(label_view);                                                        // replace spaces in name with _ (as is done in vcfg files)
            std::ranges::replace(sanitized_string, ' ', '_');
            vcfg_stream << sanitized_string << config.substr(label_name_end + 1) << '\n';
            renderer->readParameters(vcfg_stream, VOLCANITE_VERSION, true);
        }
    }
    return 0;
}

int volcanite_main(int argc, char *argv[]) {
    VolcaniteArgs args;
    std::shared_ptr<volcanite::CompressedSegmentationVolume> compressedSegmentationVolume;
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase;
    auto ret = volcanite_provide_args_and_csgv(args, compressedSegmentationVolume, csgvDatabase, argc, argv);
    if (ret != RET_SUCCESS) {
        return ret;
    }

    if (args.performDecompression()) {
        auto payload = compressedSegmentationVolume->decompress();
        auto dim = compressedSegmentationVolume->getVolumeDim();
        vvv::Volume<uint32_t> decompressed_volume{static_cast<float>(dim.x), static_cast<float>(dim.y), static_cast<float>(dim.z),
                                                  dim.x, dim.y, dim.z, vk::Format::eUndefined, *payload};
        if (!decompressed_volume.write(args.decompress_export_file))
            Logger(Error) << "volume could not be decompressed";
        else
            Logger(Info) << "volume decompressed to " << args.decompress_export_file;
    }

    if (args.export_stats) {
        Logger(Info, true) << "export brick statistics...";
        std::string stats_path = stripFileExtension(args.input_file) + "_brickstats.csv";
        csv_export(compressedSegmentationVolume->gatherBrickStatistics(), stats_path);
        Logger(Info) << "export brick statistics to " << stats_path + " done";
    }

    if (!args.headless || args.performHeadlessRendering()) {

        Logger(Info) << "--------------------------------------------------- ";
        Logger(Info) << "initializing Volcanite renderer";

        // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
        if (args.stream_lod && !compressedSegmentationVolume->isUsingSeparateDetail()) {
            Logger(Debug) << "separating detail level encoding for streaming";
            compressedSegmentationVolume->separateDetail();
            Logger(Debug) << compressedSegmentationVolume->getEncodingInfoString();
        }

        // if the attribute database is a dummy, we update the min/max attribute values for the volume labels
        if (csgvDatabase->isDummy())
            csgvDatabase->updateDummyMinMax(*compressedSegmentationVolume);

        const auto renderer = std::make_shared<volcanite::CompressedSegmentationVolumeRenderer>(!args.show_development_gui);
        renderer->setDecodingParameters({.cache_size_MB = args.cache_size_MB,
                                         .palettized_cache = args.cache_palettized,
                                         .decode_from_shared_memory = args.decode_from_shared_memory,
                                         .cache_mode = args.cache_mode,
                                         .empty_space_resolution = args.empty_space_resolution,
                                         .shader_defines = args.shader_defines});
        renderer->setCompressedSegmentationVolume(compressedSegmentationVolume, csgvDatabase);
        renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});

        // if a screenshot file, video file, or evaluation log file path is given, run the headless mode first
        if (args.performHeadlessRendering()) {

            // obtain a headless rendering engine
            auto renderEngine = HeadlessRendering::create("Volcanite", renderer, std::make_shared<DebugUtilsExt>());
            renderEngine->acquireResources();
            tryImportRenderConfigs(args, renderer);
            int prev_renderer_target_accumulation_frames = renderer->getTargetAccumulationFrames();

            // TODO: add a cmd arg for frame_time_file_out
            args.hr_cfg.frame_time_file_out = expandPath("~/vvideo/frame_time_test.csv");

            // perform a dry evaluation run first, gathering frame times etc., if required
            if (args.performHeadlessEvaluationPrepass()) {
                Logger(Info) << "Rendering Evaluation Pass:";
                auto hr_cfg = args.hr_cfg;
                hr_cfg.video_fmt_file_out = ""; // disable any video export
                renderer->startFrameTimeTracking();
                renderEngine->renderFrames(hr_cfg);
                // the renderEngine stops the frame time tracking

                // export rendering time for each frame to a .csv file if requested
                if (!args.hr_cfg.frame_time_file_out.empty()) {
                    if (auto frame_time_file = std::ofstream(args.hr_cfg.frame_time_file_out);
                        frame_time_file.is_open()) {
                        // for more detailed frame timings: csv_utils::csv_export
                        for (const float &v : renderer->getLastTrackingFrameTimes())
                            frame_time_file << v << "\n";
                        frame_time_file.close();
                        } else {
                            Logger(Warn) << "Could not export frame timings to " << args.hr_cfg.frame_time_file_out;
                        }
                }
                // add new results to evaluation log files
                if (!args.eval_logfiles.empty()) {
                    if (!renderer->writeParameterFile(stripFileExtension(args.eval_logfiles.at(0)) + ".vcfg"))
                        Logger(Warn) << "could not write vcfg file " << (stripFileExtension(args.eval_logfiles.at(0)) + ".vcfg");
                    for (const auto &eval_logfile : args.eval_logfiles) {
                        if (!EvaluationLogExport::write_eval_logfile(eval_logfile, args.eval_name, argc, argv,
                                                                     compressedSegmentationVolume->getLastEvaluationResults(),
                                                                     {}, // TODO: add decompression benchmark for evaluation logging
                                                                     renderer->getLastEvaluationResults())) {
                            Logger(Info) << "exported evaluation results to " << eval_logfile;
                                                                     } else {
                                                                         Logger(Warn) << "could not export evaluation results to " << eval_logfile;
                                                                         return RET_IO_ERROR;
                                                                     }
                    }
                }
            }

            // if a video export is rendered, do a separate pass for it since the frame downloads may affect frame timings
            if (args.performHeadlessVideoRendering()) {
                Logger(Info) << "Video Export Pass:";
                auto hr_cfg = args.hr_cfg;
                if (hr_cfg.video_frames < 0)
                    hr_cfg.video_frame_times = &renderer->getLastTrackingFrameTimes();
                // ensure that the render will converge for at least the number
                // of requested accumulation frames rendered for each output frame.
                if (renderer->getTargetAccumulationFrames() > 0u && renderer->getTargetAccumulationFrames() < args.hr_cfg.accumulation_samples)
                    renderer->setTargetAccumulationFrames(static_cast<int>(args.hr_cfg.accumulation_samples));
                renderEngine->renderFrames(hr_cfg);

                // try creating a video from the files using ffmpeg system calls
                if (args.hr_cfg.video_out_frame_rate == 0u) {
                    assert((args.hr_cfg.video_frames < 0 || renderer->getLastTrackingFrameTimes().size() == args.hr_cfg.video_frames) && "missing correct frame time tracking for video frames.");
                    try_ffmpeg_video_encoding_with_timing(args.hr_cfg.video_fmt_file_out, renderer->getLastTrackingFrameTimes());
                } else {
                    if (args.hr_cfg.video_frames >= 0)
                        Logger(Warn) << "Video export with real-time frame rate should be used with animation duration instead of frame count (--video-cfg f<-duration>)";
                    try_ffmpeg_video_encoding(args.hr_cfg.video_fmt_file_out, args.hr_cfg.video_out_frame_rate);
                }
            }

            // if a screenshot export is requested, render a new high quality frame output
            // let the render engine render all frames
            if (!args.screenshot_output_file.empty()) {
                Logger(Info) << "Screenshot (High Quality) Export Pass:";
                // ensure that a high quality screenshot is rendered (enough accumulation frames)
                args.hr_cfg.video_frames = 1;
                args.hr_cfg.accumulation_samples = renderer->getTargetAccumulationFrames();
                if (args.hr_cfg.accumulation_samples == 0)
                    args.hr_cfg.accumulation_samples = 60;
                if (auto texture = renderEngine->renderFrames(args.hr_cfg);
                    (texture != nullptr && export_texture(texture.get(), args.screenshot_output_file))) {
                    texture = {};
                } else {
                    Logger(Error) << "could not export final render frame to " << args.screenshot_output_file;
                    return RET_RENDER_ERROR;
                }
            }

            renderEngine->releaseResources();
        }

#ifndef HEADLESS
        // only start the application if we are not in headless mode
        if (!args.headless) {
            // we only need the rendering part for screenshots/videos or the interactive app
            const std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString() + "  " + compressedSegmentationVolume->getLabel();
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());

            // export the state of the renderer next to the input or csgv volume when the app is closed,
            // and pass a directory where quick access states are stored to and loaded from
            if (!args.performCompression()) {
                renderer->saveConfigOnShutdown(stripFileExtension(args.input_file) + ".vcfg");
            } else if (!args.compress_export_file.empty()) {
                renderer->saveConfigOnShutdown(stripFileExtension(args.compress_export_file) + ".vcfg");
            } else {
                renderer->saveConfigOnShutdown(args.working_dir.generic_string() + "/shutdown.vcfg");
            }
            app->setQuickConfigLocationFmt(args.working_dir.generic_string() + "/q{}.vcfg");

            app->setStartupWindowSize({args.render_resolution[0], args.render_resolution[1]}, args.fullscreen);
            app->setVSync(args.enable_vsync);
            app->acquireResources();
            tryImportRenderConfigs(args, renderer);
            return app->exec();
        }
#endif
    } else {
        // If no rendering is requested: export the copmression results here
        for (const auto &eval_logfile : args.eval_logfiles) {
            if (!EvaluationLogExport::write_eval_logfile(eval_logfile, args.eval_name, argc, argv,
                                                         compressedSegmentationVolume->getLastEvaluationResults(),
                                                         {}, // TODO: add decompression benchmark
                                                         {})) {
                Logger(Info) << "exported evaluation results to " << eval_logfile;
            } else {
                Logger(Warn) << "could not export evaluation results to " << eval_logfile;
                return RET_IO_ERROR;
            }
        }
    }

    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_main)
