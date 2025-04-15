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

#include "vvv/util/Logger.hpp"
#include "vvv/core/HeadlessRendering.hpp"
#ifdef HEADLESS
    #include "vvv/headless_entrypoint.hpp"
#else
    #include "vvvwindow/App.hpp"
    #include "vvvwindow/entrypoint.hpp"
#endif

#include "volcanite/util/args_and_csgv_provider.hpp"
#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "volcanite/compression/CSGVDatabase.hpp"
#include "volcanite/eval/EvaluationLogExport.hpp"
#include "vvv/volren/Volume.hpp"

#include <string>

using namespace volcanite;

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

    VolcaniteArgs args;
    std::shared_ptr<volcanite::CompressedSegmentationVolume> compressedSegmentationVolume;
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase;
    auto ret = volcanite_provide_args_and_csgv(args, compressedSegmentationVolume, csgvDatabase, argc, argv);
    if (ret != RET_SUCCESS) { return ret; }

    if(args.performDecompression()) {
        auto payload = compressedSegmentationVolume->decompress();
        auto dim = compressedSegmentationVolume->getVolumeDim();
        vvv::Volume<uint32_t> decompressed_volume {0, 0, 0, 0, 0, 0, vk::Format::eUndefined};
        decompressed_volume.writePayload(dim.x, dim.y, dim.z, payload);
        if (!decompressed_volume.write(args.decompress_export_file))
            Logger(ERROR) << "compressed volume could not be decompressed";
        else
            Logger(INFO) << "volume successfully decompressed and written to " << args.decompress_export_file;
    }

    if(args.export_stats) {
        Logger(INFO, true) << "export brick statistics...";
        std::string stats_path = stripFileExtension(args.input_file) + "_brickstats.csv";
        csv_export(compressedSegmentationVolume->gatherBrickStatistics(), stats_path);
        Logger(INFO) << "export brick statistics to " << stats_path + " done";
    }

    if (bool run_headless_pass = !args.screenshot_output_file.empty() || !args.video_output_fmt_file.empty();
        !args.headless || run_headless_pass) {

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

            if (!args.eval_logfiles.empty())
                renderer->startFrameTimeTracking();
            auto texture = renderEngine->renderFrames(accumulation_frames,
                                                      args.record_in_file,
                                                      args.video_output_fmt_file);
            if (!args.eval_logfiles.empty()) {
                renderer->stopFrameTimeTracking({}); // stopFrameTimeTracking is already called by renderEngine
                renderer->writeParameterFile(stripFileExtension(args.eval_logfiles.at(0)) + ".vcfg");
            }

            // export final frame
            if (!args.screenshot_output_file.empty()
                && (texture == nullptr || export_texture(texture.get(), args.screenshot_output_file))) {
                Logger(ERROR) << "could not export final render frame to " << args.screenshot_output_file;
                return RET_RENDER_ERROR;
            }
            for (const auto& eval_logfile : args.eval_logfiles) {
                if (!EvaluationLogExport::write_eval_logfile(eval_logfile, args.eval_name, argc, argv,
                                       compressedSegmentationVolume->getLastEvaluationResults(),
                                       {}, // TODO: decompression benchmark
                                       renderer->getLastEvaluationResults())) {
                    Logger(INFO) << "exported evaluation results to " << eval_logfile;
                                       } else {
                                           Logger(WARN) << "could not export evaluation results to " << eval_logfile;
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
            if (!args.performCompression())
                renderer->saveConfigOnShutdown(stripFileExtension(args.input_file) + ".vcfg");
            else if (!args.compress_export_file.empty())
                renderer->saveConfigOnShutdown(stripFileExtension(args.compress_export_file) + ".vcfg");

            // we only need the rendering part for screenshots/videos or the interactive app
            const std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString()
                                        + "  " + compressedSegmentationVolume->getLabel();
            bool vsync = true;  // TODO: vsync should be a parameter of the CompressedSegmentationVolumeRenderer config
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());
            app->setStartupWindowSize({args.render_resolution[0], args.render_resolution[1]});
            app->setVSync(args.enable_vsync);
            app->acquireResources();
            tryImportRenderConfig(args, renderer);
            return app->exec();
        }
#endif
    }
    else {
        // If no rendering is requested: export the copmression results here
        for (const auto& eval_logfile : args.eval_logfiles) {
            if (!EvaluationLogExport::write_eval_logfile(eval_logfile, args.eval_name, argc, argv,
                                   compressedSegmentationVolume->getLastEvaluationResults(),
                                   {}, // TODO: decompression benchmark
                                   {})) {
                Logger(INFO) << "exported evaluation results to " << eval_logfile;
                                   } else {
                                       Logger(WARN) << "could not export evaluation results to " << eval_logfile;
                                       return RET_IO_ERROR;
                                   }
        }
    }

    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_main)
