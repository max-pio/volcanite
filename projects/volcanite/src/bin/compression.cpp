// ToDo: compression.cpp executable is probably no longer required. Remove?

#if 0

#include <string>
#include "vvv/util/csv_utils.hpp"
#include "vvv/util/Logger.hpp"
#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"


// Test the compression result for all LoDs:
#define RUN_TEST

// Run the interactive renderer after compression:
#define RUN_APP

// Set 'empty' labels in the Big01 data set to zero:
#define SET_EMPTY_TO_ZERO

// Export statistics to CSV files for later analysis in python:
//#define EXPORT_STATS

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"

using namespace vvv;

int compression(int argc, char *argv[]) {
    // configuration -------------------
    glm::uvec3 chunk_files(0);
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000_longer/outdir/nrrd_uint32/cells_frame065_100x100x100.raw";     // complex small
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000_longer/outdir/nrrd_uint32/cells_frame065_500x500x500.raw";     // complex medium
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000/outdir/nrrd_uint32/cells_frame055.raw";                        // complex large
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/OneCell_degrade_persistentRW/14002/out0/nrrd_uint32/cells_frame024.raw"; // empty space with spot noise
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/OneCell_degrade_persistentRW/00087/out0/nrrd_uint32/cells_frame024.raw"; // empty space
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/cell_sorting/40001/output_cells_group_8-00004.hdf5";                     // 4K slice cell sorting

    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/mouse_cortex/mapped/x2y3z2.hdf5";                                               // mouse cortex single chunk
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/mouse_cortex/mapped/mouse_cortex_x2y3z2.raw";                                               // mouse cortex single chunk
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/mouse_cortex/mapped/x{}y{}z{}.hdf5"; chunk_files=glm::uvec3(0,0,1);             // mouse cortex two chunks
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/mouse_cortex/mapped/x{}y{}z{}.hdf5"; chunk_files=glm::uvec3(5,8,3);   // mouse cortex complete

    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000/outdir/nrrd_uint32/cells_frame055.raw";                        // complex large
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/fiber_polymer/a/glassfibrereinforcedpolymer_unloaded_1579x1092x1651_2umVS_labeled_16bit.hdf5";  // fiber polymer
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/../segmented_volumes/mouse_cortex/mapped/x{}y{}z{}.hdf5"; chunk_files=glm::uvec3(5,8,3);   // mouse cortex complete

    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/vti_selection/output_cells-00000.vti";
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/vti_selection/output_cells-00015.vti";
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/vti_selection/output_cells-00030.vti";
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/vti_selection/output_cells-00042.vti";
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/vti_selection/output_cells-00055.vti";


    int brick_dim = 16;                                 // size of one brick
    bool force_recompute = true;                        // do a fresh compression even if there is a precomputed file
    CompressedSegmentationVolume::RANSMode rANS_mode = vvv::CompressedSegmentationVolume::NO_RANS;  // use no rANS, rANS with one table for everything, or rANS with a second freq. table for the finest LoD
    unsigned int frequency_pass_subsampling = 8u;       // only use every n³th block in every 2nd chunk file for computing frequencies
    bool use_detail_separation = false;                 // split off the operation stream of the finest LoD for on-demand CPU to GPU streaming
    std::string appName = "Volcanite Renderer [Development Version]";
    bool vsync = true;
    // ---------------------------------


//    std::shared_ptr<Volume<uint32_t>> volume;
//    CompSegVolHandler::loadSegmentationVolumeFile(path, volume);
//    volume->write_simple_cellsinsilico(path + ".raw");
//    return 0;


    // build with cmake --build ./cmake-build-release --target compression -j 12

    std::string _rANS_str[3] = {"no rANS", "single table rANS", "double table rANS"};
    bool called_from_script = false;
    if(argc > 4) {
        // to make the evaluation of multiple compression runs easier, you can call this binary with some predefined arguments:
        // input_filepath brick_size rANS_mode (n/0, s/1, d/2)

        path = std::string(argv[1]);
        // ToDo: add commandline support for chunked files instead of hardcoding it
        if(path.find("mouse_cortex") != std::string::npos) {
            chunk_files = glm::uvec3(5,8,3);
        }
        else {
            chunk_files = glm::uvec3(0, 0, 0);
        }
        brick_dim = std::stoi(argv[2]);
        rANS_mode = vvv::CompressedSegmentationVolume::NO_RANS;
        if (argv[3][0] == '1' || argv[3][0] == 's' || argv[3][0] == 'S')
            rANS_mode = vvv::CompressedSegmentationVolume::SINGLE_TABLE_RANS;
        else if (argv[4][0] == '2' || argv[3][0] == 'd' || argv[3][0] == 'D')
            rANS_mode = vvv::CompressedSegmentationVolume::DOUBLE_TABLE_RANS;
        Logger(INFO) << "processing " << path << " " << str(chunk_files) << " b=" << brick_dim << " " << _rANS_str[rANS_mode];

        called_from_script = true;
    }

    std::string latex_table_entry;

    // Load a data set and encode it as a CompressedSegmentationVolume
    std::shared_ptr<vvv::CompressedSegmentationVolume> compressedSegmentationVolume =
        CompSegVolHandler::createCompressedSegmentationVolume(path, brick_dim, rANS_mode, use_detail_separation, force_recompute, chunk_files, frequency_pass_subsampling, false, &latex_table_entry);
    if (compressedSegmentationVolume == nullptr) {
        Logger(ERROR) << "could not create / load Compressed Segmentation Volume. Aborting.";
        return -1;
    }
    Logger(DEBUG) << compressedSegmentationVolume->getEncodingInfoString() << "\n\n";

    Logger(INFO) << " --------------------------------------------------- ";

    // construct a latex string and append to the current file
    if(called_from_script) {
        std::ofstream file("./complete_compression_log.txt", std::ios_base::out | std::ios_base::app);
        if (file.is_open()) {
            file << path << " b=" << brick_dim << " " << _rANS_str[rANS_mode] << " " << NUM_THREADS << " threads" << std::endl;
            file << latex_table_entry << std::endl;
        } else {
            Logger(WARN) << " could not output latex table entry for:";
            Logger(WARN) << path << " b=" << brick_dim << " " << _rANS_str[rANS_mode];
            Logger(WARN) << latex_table_entry;
        }
        file.close();
    }

#ifdef RUN_APP
    // create and run the interactive Application
    const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeRenderer>();
    renderer->setCompressedSegmentationVolume(compressedSegmentationVolume);
    auto app = Application::create(appName, renderer, 1.f, std::make_shared<DebugUtilsExt>());

    // execute app
    app->setVSync(vsync);
    return app->exec();
#else
    return 0;
#endif
}

ENTRYPOINT(compression)

#endif
