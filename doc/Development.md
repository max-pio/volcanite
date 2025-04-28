# Development

Disclaimer: This project is a research renderer.
Some of the code you may find may be unsafe, unoptimized, or could have low code quality.
We as the authors shall in no event be liable for any claim, damages or other liability in connection with this software
that you may encounter.
The documentation may contain insufficient or deprecated information for certain features.
A lot of the code base was extended iteratively and merged from other side projects.
Keep all of this in mind when working with the code and in case you encounter any problems.
If you have questions, feel free to contact [Max Piochowiak](mailto:max.piochowiak@kit.edu).

## Dependencies

| Required Dependency | Min. Version | Usage                                 | Ubuntu / Debian package name                                           |
|---------------------|:-------------|---------------------------------------|------------------------------------------------------------------------|
| CMake               | 3.21         | creating project build files          | `cmake`                                                                |
| Vulkan SDK          | 1.3          | Vulkan development tools and headers  | Download from [https://vulkan.lunarg.com/](https://vulkan.lunarg.com/) |
| X11 dev packages¹ ² | 1:7.7        | GLFW windowing library dependencies   | `xorg-dev`                                                             |

¹ If Volcanite is build with the CMake option [HEADLESS](Setup.md#headless-builds) set, the X11 dependencies are not
required, but the application can only be run from the command line.

² If your operating system runs wayland instead of 
X11, the [GLFW wayland dependencies](https://www.glfw.org/docs/3.3/compile.html#compile_deps_wayland) are required
instead of `xorg-dev`.

| Optional Dependency | Min. Version | Usage                                   | Ubuntu / Debian package name  |
|---------------------|:-------------|-----------------------------------------|-------------------------------|
| HDF5                | 1.10.7       | read .hdf5 segmentation volumes         | `libhdf5-dev`                 |
| VTK                 | 9.1.0        | read .vti segmentation volumes          | `libvtk9-dev`                 |
| TIFF                | 4.3.0        | read TIFF volumes                       | `libtiff-dev`                 |
| PugiXML             | 1.12.1       | parse XML data                          | `libpugixml-dev`              |
| OpenMP              | 4.5          | CPU parallelization                     | included in compiler          |


## Development Tools

* We recommend using [CLion](https://www.jetbrains.com/clion/) for development.
  If you are a student or academic researcher, you can obtain a [free educational license](https://www.jetbrains.com/community/education/) of CLion using your university E-Mail address.
  CLion should work out of the box by simply opening the source folder as a project.
  Alternatively, you can build and run the software on the commandline as explained in the [Setup](Setup.md) guide or use any other IDE.
  If you use neovim, you can create a debug configuration with meta information for editing in neovim:
```
mkdir Debug
cmake -H. -BDebug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=YES
ln -s Debug/compile_commands.json .
```

* The project contains a `.clang-format` file following a LLVM-like code style. It is located in the project root.
  To format the whole codebase try the following command:
```
find . -regex './\(volcanite\|test\|lib\)/.*\.\(cpp\|hpp\|cc\|cxx\)' -exec clang-format -verbose -style=file -i {} \;
```

*Note: If the above command-lines seem to be out of date, its always worthwhile to check the CI file since these commands
are guaranteed to work. If the commands in the CI file fail, check your environment, maybe you are missing a dependency
or your dependency is out of date?*

* For GPU performance analysis and debugging, we recommend [NVIDIA Nsight Graphics](https://developer.nvidia.com/nsight-graphics).
  Once installed, you should be able to start nsight with `ngfx-ui` from your commandline, create a new project, and select an executable of your choice from the build directory.

## Project Structure

```
| volcanite
|   \_ volcanite renderer (*/renderer)
|   \_ CSGV segmentation volume compression (*/compression) 
| python
|   \_ python utility scripts and dummy package
| test
|   \_ encoding and rendering CTest tests
| lib
|   \_ vulkan backend (vvv)
|   \_ windowing application libraries (vvv-glfw-app)
| extern
|   \_ external compile time libraries
```

The Volcanite project directory has its own [ReadMe](../volcanite/ReadMe.md) with development information about the renderer.


## Git Branching Strategy

* Each developer starts own branch names with a prefix `ab/<branch>`. Usually, `ab` are your initials.
* Each developer has their own development branch from which additional branches for features can be created.
* Merging happens to the `staging` branch first where merging bugs can be fixed. We do not rebase here.
* Ideally, we test the `staging` branch with different builds (Ubuntu, Windows, headless, ..) before release.
* If the `staging` branch feels complete and bug free, it can be merged into `main` by the repository maintainer.

```
  ab/feature   ab/development   cd/development   staging      main
      .              ┌─┐              .             .           .
      .              └┬┘             ┌─┐            .           .
      .               │              └┬┘            .           .
      .              ┌▼┐              │             .           .
      ┌──────────────┴┬┘             ┌▼┐            .           .
      │               │              └┬┘            .           .
     ┌▼┐              │               └───────────►┌─┐          .
     └┬┘ feature     ┌▼┐              .            └┬┘          .
      │  branch      └┬┘              .             │           .
     ┌▼┐              │               .            ┌▼┐          .
     └─┴────────────►┌▼┐              .            └┬┘ bugfix   .
      .              └┬┘              .             │           .
      .               │               .             │           .
      .               └───────────────────────────►┌▼┐          .
      .               .               .            └┬┘          .
      .               .               .             └─────────►┌─┐
      .               .               .                        └─┘ tag 0.1
```

## Open Source Libraries and Licenses

For a full list of directly included dependencies and their licenses
see the [LICENSE_THIRD_PARTY](../volcanite/package_assets/LICENSE_THIRD_PARTY.txt) document.  
The Volcanite code directly includes or uses code from:
* [GLFW](https://github.com/glfw/glfw), released under the Zlib license
* [GLM](https://github.com/g-truc/glm), released under the MIT license
* [HighFive](https://github.com/BlueBrain/HighFive?tab=readme-ov-file), released under the BSL-1.0 license
* [Dear ImGui](https://github.com/ocornut/imgui), released under the MIT license
* [Portable File Dialogs](https://github.com/samhocevar/portable-file-dialogs), released under the WTFPL license
* [ryg rANS](https://github.com/rygorous/ryg_rans), released by Fabian Giesen under the CC0 license
* [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect), released under the Apache-2.0 license
* [SQLiteC++](https://github.com/SRombauts/SQLiteCpp), released under the MIT license
* [stb](https://github.com/nothings/stb), released into the public domain via Unlicense
* [tclap](https://tclap.sourceforge.net/), released under the MIT license
* [MyToyRenderer](https://github.com/MomentsInGraphics/vulkan_renderer), released by Christoph Peters under the GPLv3 license 
* [Pasta Toolbox](https://github.com/pasta-toolbox/), released by Florian Kurpicz under the GPLv3 license

as well as the following assets:
* [Tileable Blue Noise Textures](http://momentsingraphics.de/BlueNoise.html) released by Christoph Peters under the CC0 license
* The [Quicksand](https://fonts.google.com/specimen/Quicksand) font released by Andrew Paglinawan under the OFL 1.1 license
* A collection of colormaps, namely
  * The [Viridis colormaps](https://bids.github.io/colormap/) released under the CC0 license
  * The Viridis [GLSL approximations](https://www.shadertoy.com/view/WlfXRN) released by Matt Zucker under the CC0 license
  * The [coolwarm colormap](https://www.kennethmoreland.com/color-advice/) released by Kenneth Mooreland into the public domain

<!---
[ToDo]

See also the [Volcanite Project ReadMe](../volcanite/ReadMe.md)

Notes to include:

* CMake build
  * selecting optional libraries
  * packaging / installing
* VVV Framework
  * Pass abstraction
  * Synchronization Primitives
  * Shader management: reflections, SPIRV compilation,
  * GUI / parameter interface
  * Shader utilities: random numbers and noise, transfer functions,
  * Volume resources and readers
* Volcanite
  * general architecture, functionality and principles
  * compressed segmentation volumes and GPU caches (Paper)
  * csgv_renderer Shader walkthrough
* Other executables
  * compression development renderer
  * brick viewer
* Debugging and Analysis
  * Enabling the Address Sanitizer
  * Using NVIDIA nsight
  * debugPrintfEXT in shaders
  * Performance Analysis
    * MiniTimer
    * GPU: nsight, ctx->debugMarker, Performance Counters, ..
    * Automation: quick and dirty tips and tricks 
-->