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

#ifdef HEADLESS
    #include "vvv/headless_entrypoint.hpp"
#else

#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp"
#include <memory>

using namespace volcanite;


int csgv_brick_viewer(int argc, char *argv[]) {

    if (argc <= 1) {
        Logger(ERROR) << "No CSGV file path provided as command line argument. Compress a volume with Volcanite first.";
        return -1;
    }

    std::string path = argv[1];
    std::string appName = "Compressed Segmentation Volume Brick Viewer";

    // Load a data set and encode it as a CompressedSegmentationVolume
    auto csgv = std::make_shared<volcanite::CompressedSegmentationVolume>(volcanite::CompressedSegmentationVolume());
    if(!csgv->importFromFile(path, true)) {
        Logger(ERROR) << "Could not import CSGV from " << path;
        return -2;
    }

    // create and run the interactive Application
    const auto renderer = std::make_shared<volcanite::CompressedSegmentationVolumeBrickViewer>();
    renderer->setCompressedSegmentationVolume(csgv);
    auto app = Application::create(appName, renderer);
    app->setVSync(true);
    return app->exec();
}

ENTRYPOINT(csgv_brick_viewer)

#endif // HEADLESS
