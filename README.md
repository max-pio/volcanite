# Volcanite Renderer

![Renderer Preview Image](doc/volcanite_app.jpg)

A research renderer for segmentation volumes implemented in Vulkan.
The implementation is based on the [Vulkan Volume Visualization](https://git.scc.kit.edu/vulkan-vol-vis/vvv) framework (vvv) by Reiner Dolp and Max Piochowiak which is currently closed source.
If not stated otherwise, this implementation is owned by Karlsruhe Institute of Technology and the authors of this repository.
Do not distribute this code or any fragments or builds of it without permission!

## Quick Start

### 1. Building the Executable

#### Ubuntu / Debian
*Tested on Ubuntu 22.04*

1. Install recent GPU drivers. Under Ubuntu, you can select recent proprietary drivers in the "Additional Drivers" GUI.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) using the SDK Installer.
3. Install all required packages:
```
sudo apt install build-essential cmake libglfw3-dev libglm-dev libtclap-dev -y
```
4. Optional: Install optional packages:
```
sudo apt install libhdf5-dev libtiff-dev libpugixml-dev libsqlite3-dev -y
```
5. Build the project. Run in project root directory:
```
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --target volcanite -j 8
```

Instead of the commandline build from step 5, you can alternatively open the CMake project with an IDE of your choice, e.g. CLion or MS Visual Studio Code.
If your IDE supports generating build files, you can directly open the `CMakeLists.txt` in the root folder.


#### Windows
*Tested on Windows 10 with CLion (gcc)*

1. Install recent GPU drivers. This should happen automatically with Windows updates. Otherwise, find them at your GPU vendor webpage.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) using the SDK Installer.
3. Install [CMake](https://cmake.org/download/) and select "Add CMake to the system PATH".
4. Install [MS Visual Studio](https://visualstudio.microsoft.com/downloads/) 2015 Update 3 or greater and select the tools for C++ desktop development: `MSVC`, `C++-CMake-Tools`, `C++ AddressSanitizer`.
5. Install the [vcpkg](https://vcpkg.io/en/getting-started) package manager. From the vcpkg install directory, install the required 64 bit packages in a powershell console:
```
.\vcpkg install glfw3 glm --triplet=x64-windows
```
6. Optional: Install optional packages:
```
.\vcpkg install hdf5 tiff sqlite3 --triplet=x64-windows
```
7. Build the project. Choose one of the following, depending on your development environment:

*Note that MSVC compilers - and thus Visual Studio - are currently not supported. Use MinGW (CLion) or GCC!*

**Visual Studio**
Integrate vcpkg into Visual Studio with the following command (may require administrator elevation):
```
.\vcpkg integrate install
```
Then you can open the project root folder in Visual Studio and build the `volcanite` executable.

**Clion**
Open the Toolchains settings (File > Settings), and go to the CMake settings (Build, Execution, Deployment > CMake). Finally, in `CMake options`, add the following line:
```
-DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]\scripts\buildsystems\vcpkg.cmake
```
You must add this line to each profile. Then you can open the `CMakeLists.txt` from the project root with CLion and build the `volcanite` executable.

**CMake** Build either using the CMake GUI or by running the following commands in the project root directory:
```
mkdir cmake-build-release
cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake .. 
cmake --build . --target volcanite -j 8
```

**Visual Studio Code** Add the following to your workspace `settings.json` and open the project with CMake Tools:
```
{
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "[path to vcpkg]/scripts/buildsystems/vcpkg.cmake"
  }
}
```
Afterwards you can build the `volcanite` executable.

### 2. Running the Program

Start the `volcanite` executable, either providing a path to a segmentation volume as a commandline argument with
```
./projects/volcanite/volcanite /path/to/your/segmentation/volume
```
or by using the file dialog to select a volume file.
Run `./volcanite --help` for a complete list of arguments and commands.
See [Supported File Formats](#Supported-File-Formats) for a list of currently usable data formats.
You can find a collection of example data sets listed in [Example_Data](doc/Example_Data.md). 

### Supported File Formats

* **Simplified NRRD**
A binary file with the following format: 
One utf8 line specifying the integer data type, followed by one line containing space separated width, height, depth.
Followed by the raw binary data.
File name must end with `.raw`.
Example:
```
uint32
128 256 128
[binary voxel data in row, column, slice order: x0y0z0 x1x0y0 ...]
```

* **Simplified VTI**
The XML image file format of the Visualization Toolkit (VTK).
File name must end with `.vti`.
Currently, only a simple subset of these image data files can be loaded as it must have the format:
```
<VTKFile type="ImageData" version=[*] byte_order=[BYTE_ORDER] header_type="UInt64">
    <ImageData WholeExtent="0 [WIDTH] 0 [HEIGHT] 0 [DEPTH]" Origin="0 0 0" Spacing="1.000000e+00 1.000000e+00 1.000000e+00">
        <CellData Scalars="[*]">
            <DataArray type="UInt32" Name="[*]" format="appended" offset="0" NumberOfComponents="1"/>
        </CellData>
    </ImageData>
    <AppendedData encoding="raw">
        [BINARY VOXEL DATA]
    </AppendedData>
</VTKFile>
```

* **HDF5**
An HDF5 file whose first object must be a dataset containing the integer voxel data in the correct shape.
This format is only available when the hdf5 library is available: 
Either by installing the package `libhdf5-dev` on Ubuntu or by using the [precompiled binary distributions](https://www.hdfgroup.org/downloads/hdf5/) from the HDF group.
File name must end with `.hdf5`.
   
---

## Development

Disclaimer: This project is a research renderer.
Some of the code you may find may be unsafe, unoptimized, or could have low code quality.
We as the authors shall in no event be liable for any claim, damages or other liability in connection with this software that you may encounter.
The documentation may contain insufficient or deprecated information for certain features.
A lot of the code base was extended iteratively and merged from other side projects.
Keep all of this in mind when working with the code and in case you encounter any problems.
If you have questions about certain parts in the implementation, feel free to contact [Max Piochowiak](mailto:max.piochowiak@kit.edu).


### Dependencies

| Required Dependency | Min. Version | Usage                          | Ubuntu / Debian package name                            |
|---------------------|:-------------|--------------------------------|---------------------------------------------------------|
| CMake               | 3.16         | creating project build files   | `cmake`                                                 |
| Vulkan SDK          | 1.3          | Vulkan development tools and headers    | Download from [https://vulkan.lunarg.com/](https://vulkan.lunarg.com/) |
| glslangValidator    | 11:12.2      | SPIR-V shader compiler         | included in drivers, alternative package`glslang-tools` |
| glm                 | 0.9.9.8      | GLSL equivalents for host code | `libglm-dev`                                            |
| GLFW                | 3.3.6        | windowing for GLFW application | `libglfw3-dev`                                          |

| Optional Dependency | Min. Version  | Usage                                   | Ubuntu / Debian package name |
|---------------------|:--------------|-----------------------------------------|------------------------------|
| HDF5                | 1.10.7        | read .hdf5 segmentation volumes         | `libhdf5-dev`                |
| SQLite              | 3.37.2        | read/write sqlite label attribute files | `libsqlite3-dev`             |
| TIFF                | 4.3.0         | read TIFF volumes                       | `libtiff-dev`                |
| PugiXML             | 1.12.1        |  parse XML data                         | `libpugixml-dev`             |
| TCLAP               | 1.2.5         | parse command line arguments            | `libtclap-dev`               |
| OpenMP              | 4.5           | CPU parallelization                     | included in compiler         |


### Development Tools

* We recommend using [CLion](https://www.jetbrains.com/clion/) for development.
You can obtain a [free educational license](https://www.jetbrains.com/community/education/) of CLion using your university E-Mail address.
CLion should work out of the box by simply opening the source folder as a project.
Alternatively, you can build and run the software on the commandline as explained in the [Quick Start](#Quick-Start) guide or use any other IDE.
If you use neovim, you can create a debug configuration with meta information for editing in neovim:
```
mkdir Debug
cmake -H. -BDebug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=YES
ln -s Debug/compile_commands.json .
```

* The research project currently lacks a clear formatting guideline.
However, a `.clang-format` file is located in the project root.
To format the whole codebase try the following command:
```
find . -not -path '*/\.*' -not -path 'Debug/*' -not -path 'build/*' -regex '.*\.\(cpp\|hpp\|cc\|cxx\)' -exec clang-format -verbose -style=file -i {} \;
```

*Note: If the above command-lines seem to be out of date, its always worthwhile to check the CI file since these commands
are guaranteed to work. If the commands in the CI file fail, check your environment, maybe you are missing a dependency
or your dependency is out of date?*

* For GPU performance analysis and debugging, we recommend [NVIDIA Nsight Graphics](https://developer.nvidia.com/nsight-graphics).
Once installed, you should be able to start nsight with `ngfx-ui` from your commandline, create a new project, and select an executable of your choice from the build directory.

### Project Structure

The general project structure resembles the vvv library structure:
```
project root
| extern
|   \_ compile time libraries
| projects
|   | examples
|   |   \_ examples for the vvv framework
|   | volcanite
|   \   \_ the volcanite renderers and other volcanite functionality
| pyvvv
|   \_ [deprecated] vvv python bindings for using the framework and renderers in python  
| vvv
|   \_ common classes and core functionality for GPU renderers that are shared among projects
| vvv-glfw-app
|   \_ windowing application to blit render outputs to the screen and to provide GUI and Camera
```

The Volcanite project directory has its own [ReadMe](projects/volcanite/ReadMe.md) with development information about the renderer.
If you want to create a new project, have a look at the [ReadMe](projects/README.md) in the `projects` directory.
The [ReadMe](vvv-glfw-app/README.md) file of the GLFW Application contains further information for using the GUI window.
Information about using the vvv library and implementing or extending GPU renderers will follow in the future.
Until then, you can have a look at the [code examples](projects/examples/src/bin) and other existing Volcanite renderers to make yourself familiar with the code base.
