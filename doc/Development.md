# Development

Disclaimer: This project is a research renderer.
Some of the code you may find may be unsafe, unoptimized, or could have low code quality.
We as the authors shall in no event be liable for any claim, damages or other liability in connection with this software that you may encounter.
The documentation may contain insufficient or deprecated information for certain features.
A lot of the code base was extended iteratively and merged from other side projects.
Keep all of this in mind when working with the code and in case you encounter any problems.
If you have questions, feel free to contact [Max Piochowiak](mailto:max.piochowiak@kit.edu).

## Dependencies

| Required Dependency | Min. Version | Usage                                | Ubuntu / Debian package name                                            |
|---------------------|:-------------|--------------------------------------|-------------------------------------------------------------------------|
| CMake               | 3.16         | creating project build files         | `cmake`                                                                 |
| Vulkan SDK          | 1.3          | Vulkan development tools and headers | Download from [https://vulkan.lunarg.com/](https://vulkan.lunarg.com/)  |
| glslangValidator    | 11:12.2      | SPIR-V shader compiler               | included in drivers, alternative package`glslang-tools`                 |
| *GLFW               | 3.3.6        | windowing for GLFW application       | `libglfw3-dev`                                                          |

*GLFW is not required when the CMake option `HEADLESS` is set

| Optional Dependency | Min. Version | Usage                                   | Ubuntu / Debian package name  |
|---------------------|:-------------|-----------------------------------------|-------------------------------|
| HDF5                | 1.10.7       | read .hdf5 segmentation volumes         | `libhdf5-dev`                 |
| VTK                 | 9.1.0        | read .vti segmentation volumes          | `libvtk9-dev`                 |
| SQLite              | 3.37.2       | read/write sqlite label attribute files | `libsqlite3-dev`              |
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

* This research renderer currently lacks a clear formatting guideline.
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

## Project Structure

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

The Volcanite project directory has its own [ReadMe](../projects/volcanite/ReadMe.md) with development information about the renderer.
See the [code examples](projects/examples/src/bin) and other existing Volcanite renderers to make yourself familiar with the code base.


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

<!---
[ToDo]

See also the [Volcanite Project ReadMe](../projects/volcanite/ReadMe.md)

Notes to include:

* CMake build
  * setting a VOLCANITE_DEFAULT_DATA_PATH
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