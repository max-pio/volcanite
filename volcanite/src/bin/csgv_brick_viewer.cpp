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

#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/util/args_and_csgv_provider.hpp"

#ifdef HEADLESS
#include "vvv/headless_entrypoint.hpp"
#else

#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp"
#include <memory>

namespace volcanite {
class CSGVDatabase;
}

using namespace volcanite;

int csgv_brick_viewer(int argc, char *argv[]) {

    VolcaniteArgs args;
    std::shared_ptr<volcanite::CompressedSegmentationVolume> csgv;
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase;
    auto ret = volcanite_provide_args_and_csgv(args, csgv, csgvDatabase, argc, argv);
    if (ret != RET_SUCCESS) {
        return ret;
    }

    const std::string appName = "Compressed Segmentation Volume Brick Viewer";

    // create and run the interactive Application
    const auto renderer = std::make_shared<CompressedSegmentationVolumeBrickViewer>();
    renderer->setCompressedSegmentationVolume(csgv);
    const auto app = Application::create(appName, renderer);
    app->setVSync(true);
    return app->exec();
}

ENTRYPOINT(csgv_brick_viewer)

#endif // HEADLESS
