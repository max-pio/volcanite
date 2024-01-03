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

#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"
#include "volcanite/compression/CSGVMetaData.hpp"

using namespace vvv;

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
    catch(std::runtime_error e) {
        Logger(ERROR) << "Render export error: " << e.what();
        return RET_RENDER_ERROR;
    }
    return 0;
}

int tryImportRenderConfig(VolcaniteArgs& args, std::shared_ptr<CompressedSegmentationVolumeRenderer> renderer) {
    // set the startup resolution
    //renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});
    // read optional config file
    if(!args.rendering_config_file.empty()) {
        std::ifstream in(args.rendering_config_file);
        if(in.is_open()) {
            if (!renderer->readParameters(in, VOLCANITE_VERSION)) {
                Logger(ERROR) << "Could not import parameters from " << args.rendering_config_file;
                return RET_INVALID_ARG;
            }
            in.close();
        }
        else {
            Logger(ERROR) << "Could not open config file " << args.rendering_config_file;
            return RET_INVALID_ARG;
        }
        Logger(DEBUG) << "Imported rendering config from " << args.rendering_config_file;
    }
    return 0;
}

int volcanite(int argc, char *argv[]) {
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

    // ToDo: we *could* check here if a previously compressed csgv with the correct parameters lies next to the input volume
    // In that case, set the input file to that csgv and set args.compress_export_file = "".
    // For comrpession, we can also try to export the csgv file to the location of the input volume, if writing there is possible,
    // and use the tmp directory only as a fallback.
    // (see getCSGVFileName in CompSegVolHandler). Move all output file logic from CompSegVolHandler either here
    // or into a spearate method in CompSegVolHandler.
    // Also think about the processing of chunked data.. Could it be necessary to still keep the getCSGVFileName and
    // force_recompute logic in the handler for that reason? Or should we create two handlers for chunked / non-chunked?

    std::shared_ptr<vvv::CompressedSegmentationVolume> compressedSegmentationVolume;
    // if we have to compress the input file (.vti/.raw/.hdf5..) we do it here
    if(args.performCompression()) {
        glm::uvec3 max_chunk_id = glm::uvec3(args.chunk_files[0], args.chunk_files[1], args.chunk_files[2]);
        if(!args.verbose)
            Logger(INFO) << "compressing segmentation volume " << args.input_file << (args.chunked ? " with max. chunks " + str(max_chunk_id) : "");

//        CSGVMetaData msgv;
//        msgv.importOrProcessVolume(args.input_file, args.chunked, max_chunk_id);

        compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(args.input_file,
                                                                                  args.compress_export_file,
                                                                                  args.brick_size, args.rANS_mode,
                                                                                  args.stream_lod, !args.chunked,
                                                                                  args.chunked, max_chunk_id,
                                                                                  args.freq_subsampling, args.verbose);
        if(args.verbose)
            Logger(INFO) << compressedSegmentationVolume->decodingInfoString() << "\n\n";
    }
    // otherwise, we load a previously decompressed volume
    else {
        compressedSegmentationVolume = std::make_shared<CompressedSegmentationVolume>();
        compressedSegmentationVolume->importFromFile(args.input_file, args.verbose);
    }

    if(args.performDecompression()) {
        // ToDo add decompression
        Logger(ERROR) << "decompression not yet supported";
        return RET_NOT_SUPPORTED;
    }

    if (compressedSegmentationVolume == nullptr) {
        Logger(ERROR) << "could not create or load Compressed Segmentation Volume. Aborting.";
        return RET_COMPR_ERROR;
    }

    // we only need the rendering part for screenshots or the interactive app
    std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString();
    if (!args.headless || !args.screenshot_output_file.empty()) {
        Logger(INFO) << "--------------------------------------------------- ";
        Logger(INFO) << "initializing Volcanite renderer";

        const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeRenderer>(!args.show_development_gui);
        renderer->setCompressedSegmentationVolume(compressedSegmentationVolume);

        // if a screenshot file is given, we first run the headless mode to export a single image (no GUI window)
        if (!args.screenshot_output_file.empty()) {
            // obtain a headless rendering engine
            auto renderEngine = HeadlessRendering::create("Volcanite", renderer, std::make_shared<DebugUtilsExt>());
            renderEngine->acquireResources();
            tryImportRenderConfig(args, renderer);
            // let the rendering converge for some frames
            auto texture = renderEngine->renderFrames(60);
            if(texture == nullptr || export_texture(texture.get(), args.screenshot_output_file)) {
                Logger(ERROR) << "internal rendering error";
                return RET_RENDER_ERROR;
            }
            texture.reset();
            texture = nullptr;
            renderEngine->releaseResources();
        }

        // only start the application if we are not in headless mode
#ifndef HEADLESS
        if (!args.headless) {
            bool vsync = true;  // ToDo: vsync should be a parameter of the CompressedSegmentationVolumeRenderer config
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());
//            app->setStartupWindowSize({args.render_resolution[0], args.render_resolution[1]});
            app->setVSync(vsync);
            app->acquireResources();
            tryImportRenderConfig(args, renderer);
            return app->exec();
        }
#endif
    }

    return RET_SUCCESS;
}

ENTRYPOINT(volcanite)
