#include <string>
#include "vvv/util/Logger.hpp"
#include "vvv/util/detect_debugger.hpp"
#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"

// run the interactive renderer after compression
#define RUN_APP

#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"


// include this last, as it includes windows.h which defines ERROR = 0
#include "portable-file-dialogs.h"

using namespace vvv;

int volcanite(int argc, char *argv[]) {

    // parse command line arguments
    VolcaniteArgs args;
    {
        auto _args = VolcaniteArgs::parseArguments(argc, argv);
        if(!_args.has_value()) {
            Logger(ERROR) << "Exiting because of invalid arguments. See volcanite --help for available commands.";
            return -2;
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
        return -1;
    }

    if (compressedSegmentationVolume == nullptr) {
        Logger(ERROR) << "could not create or load Compressed Segmentation Volume. Aborting.";
        return -1;
    }

    // we only need the rendering part for screenshots or the interactive app
    if (!args.headless || !args.screenshot_output_file.empty()) {
        Logger(INFO) << "--------------------------------------------------- ";
        Logger(INFO) << "initializing Volcanite renderer";

        const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeRenderer>(!args.show_development_gui);
        renderer->setCompressedSegmentationVolume(compressedSegmentationVolume);

        if (!args.screenshot_output_file.empty()) {
            // ToDo add screenshot export to renderer for when command line argument is set
            Logger(ERROR) << "screenshot export not yet supported";
        }

        // only start the application if we are not in headless mode
        if (!args.headless) {
            std::string appName = "Volcanite " + VolcaniteArgs::getVolcaniteVersionString();
            bool vsync = true;  // ToDo: vsync should be a parameter of the CompressedSegmentationVolumeRenderer config
            auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());
            app->setVSync(vsync);
            return app->exec();
        }
    }

    return 0;
}

ENTRYPOINT(volcanite)
