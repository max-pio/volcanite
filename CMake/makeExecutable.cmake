#  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak, and Reiner Dolp, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https:#www.gnu.org/licenses/>.

# this file defines makeVolcaniteExecutable() and installVolcaniteExecutable()-Functions, which can be used to add new executables to the VVV project.

# howto use makeVolcaniteExecutable  and installVolcaniteExecutable in your projects CMakeLists:
# 1. add source/header files with custom list variables makeVolcaniteExecutable(NAME ${HEADERS} ${SOURCES}) note: the src/bin/NAME.cpp is always added
# 2. add libraries for the executable with target_link_libraries(NAME ..) note: libvvvwindow and libvvv are always added
# 3. add custom include include directories with target_include_directories(NAME ..) note: PRIVATE include/ is always added
# 4. add additional data/ paths to installVolcaniteExecutable(NAME ${ADDITIONAL_DATA_DIRS}) note: data/ is always added.

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
# howto use makeVolcaniteExecutable in your projects CMakeLists:
# 1. add source/header files with custom list variables makeVolcaniteExecutable(NAME ${HEADERS} ${SOURCES}) note: the src/bin/NAME.cpp is always added
# 2. add libraries for the executable with target_link_libraries(NAME ..) note: libvvvwindow and libvvv are always added
# 3. add custom include include directories with target_include_directories(NAME ..) note: PRIVATE include/ is always added
# 4. ensure that all runtime data that has to be copied to the binary data directory is within your data subfolder and all shaders are in data/shader
function(makeVolcaniteExecutable name)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # add windows icon and resources
        add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/src/bin/${name}.cpp ${ARGN}
                       ${PROJECT_SOURCE_DIR}/package_assets/windows/volcanite.rc)
    else()
        add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/src/bin/${name}.cpp ${ARGN})
    endif()
    set_target_properties(${name} PROPERTIES
            # WIN32_EXECUTABLE TRUE # this hides the console window. Disabled, because we need to see the console output! maybe re-enable for distribution
            MACOSX_BUNDLE TRUE
            )
    target_link_libraries(${name} PRIVATE LibVVV::libvvv libryg-rans tclap::tclap SQLiteCpp fmt::fmt libvolcanite)
    if(NOT HEADLESS)
        target_link_libraries(${name} PRIVATE LibVVV::libvvvwindow portable_file_dialogs)
    endif()

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1
            -DEXECUTABLE_${name}=1, -DVOLCANITE_VERSION=\"${CMAKE_PROJECT_VERSION}\")

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()

    # add compile time list of paths to data/ directories:
    set(data_dirs "")
    # search for DATA_DIR property in link_library dependencies
    get_target_property(dependency_libs "${name}" LINK_LIBRARIES)
    foreach(lib IN LISTS dependency_libs)
        get_target_property(${lib}_data_dir ${lib} INTERFACE_DATA_DIR)
        if (NOT ${lib}_data_dir STREQUAL ${lib}_data_dir-NOTFOUND)
            list(APPEND data_dirs ${${lib}_data_dir})
        endif()
    endforeach()
    # add default [project]/data/ and arguments to data_dirs
    list(APPEND data_dirs ${CMAKE_CURRENT_LIST_DIR}/data)
    # list(APPEND data_dirs ${ARGN})
    message("data paths for ${name}: ${data_dirs}")
    # these are used to find data/ files when binary is run without installing or packaging
    list(JOIN data_dirs "\;" data_dirs_escaped)
    target_compile_definitions(${name} PRIVATE "-DDATA_DIRS=\"${data_dirs_escaped}\"")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # copy runtime libraries to binary on windows
        add_custom_command(TARGET ${name} POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${name}> $<TARGET_FILE_DIR:${name}>
                           COMMAND_EXPAND_LISTS)
        add_custom_command(TARGET ${name} POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WINDOWS_RUNTIME_DLLS} $<TARGET_FILE_DIR:${name}>)
    endif()
endfunction()

# same as makeExecutabe, but for libraries. Can be used to build a library from all shared project files and link that for each executable.
function(makeVolcaniteLibrary name)
    add_library(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE LibVVV::libvvv libryg-rans tclap::tclap SQLiteCpp fmt::fmt)
    if(NOT HEADLESS)
        target_link_libraries(${name} PRIVATE LibVVV::libvvvwindow portable_file_dialogs)
    endif()
    target_include_directories(${name} PUBLIC include)
    target_include_directories(${name} PUBLIC data/shader/cpp_glsl_include)

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1
            -DVOLCANITE_VERSION=\"${CMAKE_PROJECT_VERSION}\")

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()

    # add compile time list of paths to data/ directories:
    set(data_dirs "")
    # search for DATA_DIR property in link_library dependencies
    get_target_property(dependency_libs "${name}" LINK_LIBRARIES)
    foreach(lib IN LISTS dependency_libs)
        get_target_property(${lib}_data_dir ${lib} INTERFACE_DATA_DIR)
        if (NOT ${lib}_data_dir STREQUAL ${lib}_data_dir-NOTFOUND)
            list(APPEND data_dirs ${${lib}_data_dir})
        endif()
    endforeach()
    # add default [project]/data/ and arguments to data_dirs
    list(APPEND data_dirs ${CMAKE_CURRENT_LIST_DIR}/data)
    # list(APPEND data_dirs ${ARGN})
    message("data paths for ${name}: ${data_dirs}")
    # these are used to find data/ files when binary is run without installing or packaging
    list(JOIN data_dirs "\;" data_dirs_escaped)
    target_compile_definitions(${name} PRIVATE "-DDATA_DIRS=\"${data_dirs_escaped}\"")
endfunction()

# This will add install()-definitions for this executable. This includes copying all dependent data/-Folders upon
# ninja install` or packaging the data/-Files with `cpack`.
# Also, required variables for finding the data/-Folders at runtime is passed as compile definitions.
# Ensure that target_link_libraries() is executed before this function as these libraries are searched for data/-Directories.
function(installVolcaniteExecutable name)
    # check if Volcanite is installed and if a shader compiler is included at compile time
    if(CMAKE_INSTALL_COMPONENT OR (CMAKE_INSTALL_PREFIX AND (CMAKE_INSTALL_DO_STRIP OR CMAKE_INSTALL_DO_INSTALL)))
        if(USE_SYSTEM_GLSLANG)
            message(FATAL_ERROR "USE_SYSTEM_GLSLANG is ON. Installing targets is forbidden with this option as
                    the resulting binary might not be able to compile shaders on other systems.")
        endif()
    endif()
    set(data_dirs "")

    # get all INTERFACE_DATA_DIR properties to copy those data dirs into the project install directory
    get_target_property(dependency_libs "${name}" LINK_LIBRARIES)
    foreach(lib IN LISTS dependency_libs)
        get_target_property(${lib}_data_dir ${lib} INTERFACE_DATA_DIR)
        if (NOT ${lib}_data_dir STREQUAL ${lib}_data_dir-NOTFOUND)
            list(APPEND data_dirs ${${lib}_data_dir})
        endif()
    endforeach()
    # add default [project]/data/ and arguments to data_dirs
    list(APPEND data_dirs ${CMAKE_CURRENT_LIST_DIR}/data)
    list(APPEND data_dirs ${ARGN})

    # get project name from current folder name for install rules
    get_filename_component(project_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)

    # install all data dirs to [project]/data/
    foreach(path IN LISTS data_dirs)
        install(DIRECTORY ${path} DESTINATION ${CMAKE_INSTALL_BINDIR}/ COMPONENT applications)
    endforeach()

    # install binary to target folder
    install(TARGETS ${name} DESTINATION ${CMAKE_INSTALL_BINDIR}/ COMPONENT applications)

    # install license file
    install(FILES ${PROJECT_SOURCE_DIR}/package_assets/LICENSE.txt DESTINATION ./ COMPONENT applications)
    # install 3rd-party license files
    install(FILES ${PROJECT_SOURCE_DIR}/package_assets/LICENSE_THIRD_PARTY.txt DESTINATION ./ COMPONENT applications)

    # system dependent configuration
    set(VOLCANITE_EXECUTABLE_NAME ${name})
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")

        # install .desktop entry and application icon
        configure_file(
                ${PROJECT_SOURCE_DIR}/package_assets/linux/Volcanite.desktop.in
                ${CMAKE_CURRENT_BINARY_DIR}/${VOLCANITE_EXECUTABLE_NAME}.desktop)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${VOLCANITE_EXECUTABLE_NAME}.desktop
                DESTINATION share/applications COMPONENT applications)
        install(FILES ${PROJECT_SOURCE_DIR}/package_assets/icons/volcanite_icon_256.png
                DESTINATION share/icons
                RENAME ${VOLCANITE_EXECUTABLE_NAME}_icon.png COMPONENT applications)

        # add postinst, prerm scripts for CPACK_DEBIAN_APPLICATIONS_PACKAGE_CONTROL_EXTRA to create .desktop and binary symlinks
        configure_file(
                ${PROJECT_SOURCE_DIR}/package_assets/linux/shortcut_postinst.in
                ${CMAKE_CURRENT_BINARY_DIR}/postinst)
        configure_file(
                ${PROJECT_SOURCE_DIR}/package_assets/linux/shortcut_prerm.in
                ${CMAKE_CURRENT_BINARY_DIR}/prerm)

    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # install system runtime libraries
        include(InstallRequiredSystemLibraries)
        install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)

        # install additionally specified runtime dll files
        install(FILES $<TARGET_RUNTIME_DLLS:${name}>
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
        install(FILES ${WINDOWS_RUNTIME_DLLS}
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
    endif()

endfunction()

