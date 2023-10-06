# Adding new Projects

All projects are subfolders of `projects/` and structured like this:

```
<project_name>
 \_ data
   \_ shader
 \_ include
 \_ src
   \_ bin
 CMakeLists.txt
```

If you add a new project, first create a new subdirectory in `projects/` with the above structure and add the project as a submodule in the base `../../CMakeLists.txt`.
You can initialize the project's `CMakeLists.txt` with this minimal example:

```CMake
cmake_minimum_required(VERSION 3.16)
project(<project> VERSION 0.1.0 DESCRIPTION "[...]" LANGUAGES CXX)

# find additional libraries that are not part of libvvv and libvvvwindow
# find_package([...])

set(<project>_HEADERS [...])

set(<project>_SOURCES [...])

makeExecutable(<executable> ${<project>_HEADERS} ${<project>_SOURCES})
# target_link_libraries(<executable> PRIVATE [...])
```

### Runtime Data and Shaders
All runtime data that will be copied to the subdirectory `data/` of the binary location must be placed in the project's `data/` directory.
All shaders must be placed in the `data/shader/` directory.

### Executables
`<executable>` must be the name of a main file at `src/bin/<executable>.cpp`.
One project can have multiple executable build targets.

The function `makeExectuable` is defined in the main `CMakeLists.txt` and does the following:
* creates the executable target with `src/bin/<executable>.cpp` and the additional arguments as source files.
* adds default compile definitions to the target including `EXECUTABLE_${name}` and `EXE_${NAME}_DIR`
* links the `libvvv` and `libvvvwindow` libraries to the target
* adds the project's `include` subdirectory to the target's include directories
* adds a dependency to the respective copy-data-* dummy target that copies all contents of this project's `data` directory to the binary `data` path.

### Additional Libraries
If you use libraries that are not already part of the public interface of `libvvv` or `libvvvwindow` you can import them and add them to all relevant targets that were created with `makeExecutable`.