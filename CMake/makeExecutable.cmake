# this file defines makeExecutable() and installExecutable()-Functions, which can be used to add new executables to the VVV project.

# howto use makeExecutable  and installExecutable in your projects CMakeLists:
# 1. add source/header files with custom list variables makeExecutable(NAME ${HEADERS} ${SOURCES}) note: the src/bin/NAME.cpp is always added
# 2. add libraries for the executable with target_link_libraries(NAME ..) note: libvvvwindow and libvvv are always added
# 3. add custom include include directories with target_include_directories(NAME ..) note: PRIVATE include/ is always added
# 4. add additional data/ paths to installExecutable(NAME ${ADDITIONAL_DATA_DIRS}) note: data/ is always added.

# Add a new executable which uses libvvv and libvvvwindow.
## adds a dependency for a new custom copy target to the existing target name which copies the /data subfolder of the current current list directory to the binary data directory.
## Copy the data directory shared by all subprojects to the output
#set(DATA_INCLUDE_DIR ${CMAKE_BINARY_DIR}/data)
#set(SHADER_INCLUDE_DIR ${DATA_INCLUDE_DIR}/shader)
#function(addCopyDataDependency target data_dir)
#    string(REGEX REPLACE "/" "-" copy_target_name ${data_dir})
#    string(PREPEND copy_target_name "copy-data")
#    # create the copy target for the data directory if it doesn't exist
#    if(NOT TARGET ${copy_target_name})
#        if (SKBUILD)
#            add_custom_target(${copy_target_name} ALL
#                    COMMAND ${CMAKE_COMMAND} -E copy_directory
#                    ${data_dir}
#                    ${CMAKE_SOURCE_DIR}/pyvvv/src/vvv/data
#                    )
#        else ()
#            add_custom_target(${copy_target_name} ALL
#                    COMMAND ${CMAKE_COMMAND} -E copy_directory
#                    ${data_dir}
#                    ${DATA_INCLUDE_DIR}
#                    )
#        endif ()
#        set_target_properties(${copy_target_name} PROPERTIES FOLDER DataCopyTargets)
#    endif()
#    add_dependencies(${target} ${copy_target_name})
#endfunction()


# Add executables from subdirectories
# howto use makeExecutable in your projects CMakeLists:
# 1. add source/header files with custom list variables makeExecutable(NAME ${HEADERS} ${SOURCES}) note: the src/bin/NAME.cpp is always added
# 2. add libraries for the executable with target_link_libraries(NAME ..) note: libvvvwindow and libvvv are always added
# 3. add custom include include directories with target_include_directories(NAME ..) note: PRIVATE include/ is always added
# 4. ensure that all runtime data that has to be copied to the binary data directory is within your data subfolder and all shaders are in data/shader
function(makeExecutable name)
    add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/src/bin/${name}.cpp ${ARGN})
    set_target_properties(${name} PROPERTIES
            # WIN32_EXECUTABLE TRUE # this hides the console window. Disabled, because we need to see the console output! maybe re-enable for distribution
            MACOSX_BUNDLE TRUE
            )
    target_link_libraries(${name} PRIVATE LibVVV::libvvv)
    if(NOT HEADLESS)
        target_link_libraries(${name} PRIVATE LibVVV::libvvvwindow)
    endif()
    target_include_directories(${name} PRIVATE include)

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1
            -DEXECUTABLE_${name}=1)

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()
endfunction()

# same as makeExecutabe, but for libraries. Can be used to build a library from all shared project files and link that for each executable.
function(makeLibrary name)
    add_library(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE LibVVV::libvvv)
    if(NOT HEADLESS)
        target_link_libraries(${name} PRIVATE LibVVV::libvvvwindow)
    endif()
    target_include_directories(${name} PRIVATE include)

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1)

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()
endfunction()

# This will add install()-definitions for this executable. This includes copying all dependent data/-Folders upon `ninja install` or packaging the data/-Files with `cpack`.
# Also, required variables for finding the data/-Folders at runtime is passed as compile definitions.
# Ensure that target_link_libraries() is executed before this function as these libraries are searched for data/-Directories.
function(installExecutable name)
    set(data_dirs "")

    # search for DATA_DIR property in link_library dependencies
    get_target_property(dependency_libs "${name}" LINK_LIBRARIES)
    foreach(lib IN LISTS dependency_libs)
        get_target_property(${lib}_data_dir ${lib} DATA_DIR)
        if (NOT ${lib}_data_dir STREQUAL ${lib}_data_dir-NOTFOUND)
            list(APPEND data_dirs ${${lib}_data_dir})
        endif()
    endforeach()

    # add default [project]/data/ and arguments to data_dirs
    list(APPEND data_dirs ${CMAKE_CURRENT_LIST_DIR}/data)
    list(APPEND data_dirs ${ARGN})

    message("data paths for ${name}: ${data_dirs}")

    # these are used to find data/ files when binary is run without installing or packaging
    list(JOIN data_dirs "\;" data_dirs_escaped)
    target_compile_definitions(${name} PRIVATE "-DDATA_DIRS=\"${data_dirs_escaped}\"")

    # get project name from current folder name for install rules
    get_filename_component(project_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)

    # install all data dirs to [project]/data/
    foreach(path IN LISTS data_dirs)
        install(DIRECTORY ${path} DESTINATION ${project_dir_name})
    endforeach()

    # install binary to target folder
    install(TARGETS ${name} DESTINATION ${project_dir_name})

    # use fixup_bundle to copy required dlls for windows
    set(APPS \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${project_dir_name}/${name}${CMAKE_EXECUTABLE_SUFFIX})
    install(CODE "
        include(BundleUtilities)
        message(\"fixup_bundle(\\\"${APPS}\\\")\")
        fixup_bundle(\"${APPS}\" \"\" \"\")"
        DESTINATION .)
endfunction()
