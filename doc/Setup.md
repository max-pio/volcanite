# Volcanite Setup Guide

Most of the dependencies are included as header only libraries.
Only the Vulkan SDK, CMake and the GLFW libraries have to be installed.

Note: If your system does not provide any windowing (e.g. a remote server without a desktop environment) or the GLFW libraries
are not available, you can build the Volcanite project with the CMake option `HEADLESS` set. 
See [Headless Builds](#headless-builds).

## Ubuntu / Debian
*Tested on Ubuntu 22.04*

1. Install recent GPU drivers. Under Ubuntu, you can select recent proprietary drivers in the "Additional Drivers" GUI.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) using the SDK Installer.
3. Install all required packages:
```
sudo apt install build-essential cmake xorg-dev -y
```
4. Optional: Install optional packages:
```
sudo apt install libhdf5-dev libvtk9-dev libtiff-dev libpugixml-dev libsqlite3-dev -y
```
5. Build the project. Run in project root directory:
```
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --target volcanite
```

Instead of the commandline build from step 5, you can alternatively open the CMake project with an IDE of your choice, e.g. CLion or MS Visual Studio Code.
If your IDE supports generating build files, you can directly open the `CMakeLists.txt` in the root folder.


## Windows
*Tested on Windows 10 with CLion (MinGW)*

1. Install recent GPU drivers. This should happen automatically with Windows updates. Otherwise, find them at your GPU vendor webpage.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) using the SDK Installer.
3. Install [CMake](https://cmake.org/download/) and select "Add CMake to the system PATH".
4. *Optional: Install packages to support a wider range of volume file formats. 
   Install the [vcpkg](https://vcpkg.io/en/getting-started) package manager. From the vcpkg install directory, install the optional 64 bit packages in a powershell console:*
```
.\vcpkg install hdf5 vtk tiff sqlite3 --triplet=x64-windows
```
5. Build the project. Choose one of the following, depending on your development environment:


**CMake** Build either using the CMake GUI or by running the following commands in the project root directory:
```
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release && cmake --build . --target volcanite
```

If you use vkpcg, you have to pass the toolchain file to CMake with:
```
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --target volcanite
```

**Visual Studio** 

*MSVC compilers - and thus Visual Studio - are currently not supported as they do not support current OpenMP directives.
Use MinGW (CLion) or GCC compilers instead.*

<!--
* Install [MS Visual Studio](https://visualstudio.microsoft.com/downloads/) 2015 Update 3 or greater and select the tools for C++ desktop development: `MSVC`, `C++-CMake-Tools`, `C++ AddressSanitizer`.

* If you use vcpkg, integrate vcpkg into Visual Studio with the following command (may require administrator elevation):
```
.\vcpkg integrate install
```
* Open the project root folder in Visual Studio and build the `volcanite` executable.
-->

**Clion**
Open the `CMakeLists.txt` from the project root with CLion and build the `volcanite` executable.

If you use vcpkg, you must include the toolchain file before building.
Open the Toolchains settings (File > Settings), and go to the CMake settings (Build, Execution, Deployment > CMake). Finally, in `CMake options`, add the following line:
```
-DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]\scripts\buildsystems\vcpkg.cmake
```
You must add this line to each profile.

**Visual Studio Code**
Open the project with CMake Tools and build the `volcanite` executable.

If you use vkpcg, add the following to your workspace `settings.json` before building:
```
{
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "[path to vcpkg]/scripts/buildsystems/vcpkg.cmake"
  }
}
```

## Headless Builds

It is possible to run Volcanite from the command line only, without opening any application window. 
This is useful on a machine where no windowing system is present (e.g. a remote server) or for automating Volcanite operations from scripts.
To start Volcanite without opening a window, pass the optional command line argument `--headless`.

Volcanite can be built without any windowing system and GUI window dependencies by enabling the CMake option `HEADLESS`, e.g. with
```
cmake -DHEADLESS=ON -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --target volcanite
```
In this case, the `xorg-dev` package is not required.
GPU drivers and the Vulkan SDK still need to be available and Volcanite can only be run with the `--headless` argument.


## FAQ

How do I run Volcanite on a system without a GUI (e.g. a headless remote server) or where the `xorg-dev` package is not available?
* build Volcanite in [headless mode](#headless-builds).


I am using Wayland instead of X11 on Linux. How do I build Volcanite?
* In the default configuration, the GLFW library is only build with X11 support under Linux / Unix.
  If you run wayland, you need to set the CMake variable `GLFW_BUILD_WAYLAND` to `ON` and install the requried packages
  for wayland (see [GLFW 3.3 Compile Guide](https://www.glfw.org/docs/3.3/compile_guide.html#compile_deps)). 