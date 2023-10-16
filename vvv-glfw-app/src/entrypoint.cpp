#include <vvvwindow/entrypoint.hpp>

#include <vvv/util/detect_debugger.hpp>
#include <vvv/util/Paths.hpp>
#include <vvv/util/Logger.hpp>

#ifdef _WIN64
#include <Windows.h>
#endif

#include <iostream>
#include <string>

int entrypoint_main(int(*main)(int, char**), int argc, char **argv, const std::string& dataDirs) {
    /* print uncaught exceptions before segmentation fault. But don't do this when a debugger is attached, otherwise the stacktrace is lost. */
    if (!vvv::debuggerIsAttached()) {
        try {
            vvv::Paths::initPaths(dataDirs);
            int ret = main(argc, argv);

            #ifdef _WIN64
            std::cout << "Application exit with return code " << ret << ". Press any key to close." << std::endl;
            _getwch();
            #endif

            return ret;
        } catch (const std::exception &exc) {
            using namespace vvv;
            Logger(ERROR) << "An exception occurred: " << exc.what();
            #ifdef _WIN64
            MessageBoxA(NULL, exc.what(), "An exception occurred.", MB_OK | MB_ICONERROR);
            #endif

            throw exc;
        }
    } else {
        vvv::Logger(vvv::DEBUG) << "Running in DEBUG mode";
        vvv::Paths::initPaths(dataDirs);
        return main(argc, argv);
    }
}