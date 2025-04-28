//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#ifdef _WIN32
#include <Windows.h>
#include <iostream>
#endif

#include <vvv/headless_entrypoint.hpp>

#include <vvv/util/Logger.hpp>
#include <vvv/util/Paths.hpp>
#include <vvv/util/detect_debugger.hpp>

#include <string>

int entrypoint_main(int (*main)(int, char **), int argc, char **argv, const std::string &dataDirs) {
    /* print uncaught exceptions before segmentation fault. But don't do this when a debugger is attached, otherwise the stacktrace is lost. */
    if (!vvv::debuggerIsAttached()) {
        try {
            vvv::Paths::initPaths(dataDirs);
            int ret = main(argc, argv);

#ifdef _WIN32
            std::cout << "Application exit with return code " << ret << ". Press any key to close." << std::endl;
            _getwch();
#endif

            return ret;
        } catch (const std::exception &exc) {
            using namespace vvv;
            Logger(Error) << "An exception occurred: " << exc.what();
            throw;
        }
    } else {
        vvv::Logger(vvv::Debug) << "Running in DEBUG mode";
        vvv::Paths::initPaths(dataDirs);
        return main(argc, argv);
    }
}