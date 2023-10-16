#include <string>
#include "vvv/util/Logger.hpp"
#include "vvv/util/detect_debugger.hpp"
#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"

// run the interactive renderer after compression
#define RUN_APP

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"

// include this last, as it includes windows.h which defines ERROR = 0
#include "portable-file-dialogs.h"

using namespace vvv;

int compression(int argc, char *argv[]) {

    if(!vvv::debuggerIsAttached())
        Logger::s_minLevel = INFO;

    // configuration -------------------
    glm::uvec3 chunk_files(0);  // max. xyz index of chunk files. e.g. (1,3,0) would load 8 chunk files.
    std::string path;           // path of segmentation volume. For chunked files, three {} are replaced with chunk ids.
    int brick_dim = 16;                                                         // size of one brick
    bool force_recompute = false;                                                // do a fresh compression even if there is a precomputed file
    CompressedSegmentationVolume::RANSMode rANS_mode = CompressedSegmentationVolume::RANSMode::NO_RANS;  // use no rANS, rANS with one table for everything, or rANS with a second freq. table for the finest LoD
    unsigned int frequency_pass_subsampling = 8u;                               // only use every n³th block in every 2nd chunk file for computing frequencies
    bool use_detail_separation = false;                                         // split off the operation stream of the finest LoD for on-demand CPU to GPU streaming
    std::string appName = "Volcanite Renderer";
    bool vsync = true;
    // ---------------------------------

    // build with cmake --build ./cmake-build-release --target volcanite -j 12

    // ToDo: replace with a cleaner and more powerful command line parser
    std::string _rANS_str[3] = {"no rANS", "single table rANS", "double table rANS"};
    if(argc > 1) {
        // to make the evaluation of multiple compression runs easier, you can call this binary with some predefined arguments:
        // input_filepath brick_size rANS_mode (n/0, s/1, d/2)

        path = std::string(argv[1]);
        if(argc > 2)
            brick_dim = std::stoi(argv[2]);
        if(argc > 3) {
            rANS_mode = vvv::CompressedSegmentationVolume::NO_RANS;
            if (argv[3][0] == '1' || argv[3][0] == 's' || argv[3][0] == 'S')
                rANS_mode = vvv::CompressedSegmentationVolume::SINGLE_TABLE_RANS;
            else if (argv[3][0] == '2' || argv[3][0] == 'd' || argv[3][0] == 'D')
                rANS_mode = vvv::CompressedSegmentationVolume::DOUBLE_TABLE_RANS;
        }
        Logger(INFO) << "processing " << path << " " << " b=" << brick_dim << " " << _rANS_str[rANS_mode];
    }
    else {
        Logger(INFO) << "call with 'volcanite [path_to_volume]' to compress and visualize a segmentation volume.";
        Logger(INFO) << "  compression options:        'segvolvis [path_to_vti] [brick_dimension {8|16|32|64}] [rANS_mode {n|s|d}]";
        Logger(INFO) << "  best rendering performance: 'segvolvis [path_to_vti] 8 n";
        Logger(INFO) << "  best compression rate:      'segvolvis [path_to_vti] 64 d";

        if (!pfd::settings::available())
        {
            Logger(ERROR) << "Portable File Dialogs are not available on this platform. Aborting.";
            return -1;
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::open_file("Choose Segmentation Volume to open", pfd::path::home(),
                                            { "Segmentation Volumes (.vti .hdf5 .raw)", "*.vti *.hdf5 *.raw", "All Files", "*" });
        if(selected_file.result().empty()) {
            Logger(ERROR) << "No segmentation volume file was provided. Aborting.";
            return -1;
        }

        path = selected_file.result().at(0);
    }

    // Load a data set and encode it as a CompressedSegmentationVolume
    std::shared_ptr<vvv::CompressedSegmentationVolume> compressedSegmentationVolume =
        CompSegVolHandler::createCompressedSegmentationVolume(path, brick_dim, rANS_mode, use_detail_separation, force_recompute, chunk_files, frequency_pass_subsampling, false);
    if (compressedSegmentationVolume == nullptr) {
        Logger(ERROR) << "could not create / load Compressed Segmentation Volume. Aborting.";
        return -1;
    }
    Logger(DEBUG) << compressedSegmentationVolume->decodingInfoString() << "\n\n";

    Logger(INFO) << " --------------------------------------------------- ";

    // create and run the interactive Application
    const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeRenderer>(true);
    renderer->setCompressedSegmentationVolume(compressedSegmentationVolume);
    auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());

    // execute app
    app->setVSync(vsync);
    return app->exec();
}

ENTRYPOINT(compression)
