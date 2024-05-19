// ToDo: Update csgv_brick_viewer.cpp and gracefully exclude in HEADLESS build
#ifndef HEADLESS

#include <string>
#include "vvv/util/Logger.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "vvv/volren/Volume.hpp"
#include "vvvwindow/App.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp"
#include <memory>

using namespace vvv;


int csgv_brick_viewer(int argc, char *argv[]) {
    // configuration -------------------
    std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000_longer/outdir/nrrd_uint32/cells_frame065_100x100x100.raw";     // complex small
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000_longer/outdir/nrrd_uint32/cells_frame065_500x500x500.raw";     // complex medium
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/Big01/000/outdir/nrrd_uint32/cells_frame055.raw";                          // complex large
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/OneCell_degrade_persistentRW/14002/out0/nrrd_uint32/cells_frame024.raw"; // empty space with spot noise
    //std::string path = std::string(VOLCANITE_DEFAULT_DATA_PATH) + "/OneCell_degrade_persistentRW/00087/out0/nrrd_uint32/cells_frame024.raw"; // empty space
    int brick_dim = 16;
    std::string appName = "Compressed Segmentation Volume Brick Viewer";
    // ---------------------------------

    Application::logLibraryAvailabilty();

    // Load a data set and encode it as a CompressedSegmentationVolume
    std::shared_ptr<Volume<uint32_t>> volume = Volume<uint32_t>::load_volcanite_raw(path);
    glm::ivec3 volume_dim(volume->dim_x, volume->dim_y, volume->dim_z);
    Logger(INFO) << path + " loaded with dim " << str(volume_dim);

    std::shared_ptr<vvv::CompressedSegmentationVolume> compression = std::make_shared<vvv::CompressedSegmentationVolume>(vvv::CompressedSegmentationVolume());

    // Perform the encoding
    // we try to load a previously exported Compressed Segmentation Volume for this file if possible, and export the compression otherwise
    // @ToDo does this have to have rANS mode set to NO_RANS?
    if(!compression->importFromFile(CompressedSegmentationVolume::getCSGVFileName(path, brick_dim, vvv::NO_RANS, false))) {
        Logger(ERROR) << "Compressed Segmentation Volume file does not yet exist!";
        return 0;
    }

    // create and run the interactive Application
    const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeBrickViewer>();
    renderer->setCompressedSegmentationVolume(compression);
    auto app = Application::create(appName, renderer);

    // the application manages one GuiInterface object which contains all GUI elements
    auto gui = app->getGui();

    // execute app
    app->setVSync(true);
    return app->exec();
}

ENTRYPOINT(csgv_brick_viewer)

#endif // HEADLESS
