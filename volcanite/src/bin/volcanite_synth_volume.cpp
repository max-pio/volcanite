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

#include "vvv/volren/Volume.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/utility/segmentation_volume_synthesis.hpp"

#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "volcanite/compression/CSGVDatabase.hpp"

using namespace volcanite;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_NOT_SUPPORTED = 2;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;


int export_texture(Texture* tex, const std::string export_file_path) {
    try {
        Logger(INFO) << "Exporting render output to " << export_file_path;
        tex->writeFile(export_file_path);
    }
    catch(const std::runtime_error& e) {
        Logger(ERROR) << "Render export error: " << e.what();
        return RET_RENDER_ERROR;
    }
    return 0;
}


int volcanite_synth_volume_main(int argc, char *argv[]) {
    VolcaniteArgs args;
    {
        auto _args = VolcaniteArgs::parseArguments(argc, argv, false);
        if(!_args.has_value()) {
            Logger(ERROR) << "Exiting because of invalid arguments. See volcanite_synth_volume --help for available commands.";
            return RET_INVALID_ARG;
        }
        args = _args.value();
    }

    if(!vvv::debuggerIsAttached() && !args.verbose)
        Logger::s_minLevel = INFO;

    std::shared_ptr<volcanite::CompressedSegmentationVolume> compressedSegmentationVolume = std::make_shared<volcanite::CompressedSegmentationVolume>();
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase = std::make_shared<volcanite::CSGVDatabase>();
    csgvDatabase->createDummy();

    // Create Synthetic Volume and Compress
    glm::uvec3 volume_dim = {100, 80, 95};
    auto volume = createDummySegmentationVolume(volume_dim);

    // TODO: parse command line arguments
    size_t operation_freq[32];
    compressedSegmentationVolume->setCompressionOptions64(32, NIBBLE_ENC);
    compressedSegmentationVolume->compressForFrequencyTable(volume.dataConst(), volume_dim, operation_freq, 2, true, false);
    compressedSegmentationVolume->setCompressionOptions64(32, DOUBLE_TABLE_RANS_ENC, operation_freq, operation_freq + 16);
    compressedSegmentationVolume->compress(volume.dataConst(), volume_dim, false);


    // we only need the rendering part for screenshots or the interactive app
    std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString();
    if (!args.headless || !args.screenshot_output_file.empty()) {
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
        renderer->setDecodingParameters(args.cache_size_MB, args.cache_palettized);
        renderer->setCompressedSegmentationVolume(compressedSegmentationVolume, csgvDatabase);

        // if a screenshot file is given, we first run the headless mode to export a single image (no GUI window)
        if (!args.screenshot_output_file.empty()) {
            // obtain a headless rendering engine
            auto renderEngine = HeadlessRendering::create("Volcanite", renderer, std::make_shared<DebugUtilsExt>());
            renderEngine->acquireResources();
            // let the rendering converge for some frames (if specified in the rendering config, we use that number)
            int accumulation_frames = renderer->getTargetAccumulationFrames();
            auto texture = renderEngine->renderFrames(accumulation_frames > 0 ? accumulation_frames : 60);
            if(texture == nullptr || export_texture(texture.get(), args.screenshot_output_file)) {
                Logger(ERROR) << "internal rendering error";
                return RET_RENDER_ERROR;
            }
            texture.reset();
            texture = nullptr;
            renderEngine->releaseResources();
        }

#ifndef HEADLESS
        // only start the application if we are not in headless mode
        if (!args.headless) {
            // export the state of the renderer next to the csgv volume when the app is closed
            if(!args.performCompression())
                renderer->saveConfigOnShutdown(stripFileExtension(args.input_file) + ".vcfg");
            else if(!args.compress_export_file.empty())
                renderer->saveConfigOnShutdown(stripFileExtension(args.compress_export_file) + ".vcfg");

            bool vsync = true;  // TODO: vsync should be a parameter of the CompressedSegmentationVolumeRenderer config
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());
            app->setStartupWindowSize({args.render_resolution[0], args.render_resolution[1]});
            app->setVSync(vsync);
            app->acquireResources();
            return app->exec();
        }
#endif
    }

    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_synth_volume_main)
