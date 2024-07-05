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

// ToDo: move hello_volume_renderer.cpp to examples
#ifndef HEADLESS

#include <string>
#include <memory>
#include "vvv/util/Logger.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "vvv/volren/Volume.hpp"
#include "vvvwindow/App.hpp"

#include "volcanite/renderer/HelloVolumeRenderer.hpp"


using namespace vvv;

int hello_volume_renderer(int argc, char *argv[]) {

    // configuration -------------------
    std::string appName = "Hello Volume Renderer";
    // ---------------------------------

    Application::logLibraryAvailabilty();

    // Create a data set
    Volume<uint32_t> volume = Volume<uint32_t>(1.f, 1.f, 1.f, 1, 1, 1, vk::Format::eR32Uint);

    // create and run the renderer and interactive application
    // ToDo: create a real dummy data set and use it in the HelloVolumeRenderer
    const auto renderer = std::make_shared<vvv::HelloVolumeRenderer>();
    auto app = Application::create(appName, renderer);

    // the application manages one GuiInterface object which contains all GUI elements
    auto gui = app->getGui();

    // execute application
    app->setVSync(true);
    return app->exec();
}

ENTRYPOINT(hello_volume_renderer)

#endif // HEADLESS
