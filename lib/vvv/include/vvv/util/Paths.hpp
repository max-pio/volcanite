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

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vvv {

/// This class has a collection of static methods to manage searched paths.
/// Paths are initialized by initPaths(), which is called from main().
class Paths {
  public:
    /// Search for a data file in any of the configured data/ folders.
    /// @param path filename and path inside the data/ folder.
    /// @return a usable path to the requested file.
    /// @throws std::runtime_error when the file is not found
    static std::filesystem::path findDataPath(const std::string &path);
    static bool hasDataPath(const std::string &path);
    static std::vector<std::filesystem::path> getDataDirectories();

    /// calls findDataPath("shader/[filename]") to search for the file containing this shader
    static std::filesystem::path findShaderPath(const std::string &filename);
    static std::vector<std::filesystem::path> getShaderDirectories();

    static std::filesystem::path getTempFileWithName(const std::string &name);
    static std::filesystem::path getTempFileForDataPath(const std::filesystem::path &dataPath);

    /// returns a file path in a subfolder of the given data path
    static std::filesystem::path getLocalFileForDataPath(const std::filesystem::path &dataPath);

    /// returns the home directory of the current user
    static std::filesystem::path getHomeDirectory();

    /// initialize paths.
    /// @param dataPath a list of semi-colon separated paths to data/-Folders. This list is sorted from lowest to highest priority.
    static void initPaths(const std::string &dataPaths);

    /// Add new dataPath to list of searched data paths.
    /// Consider instead of adding your path here to include it within CMake using either installVolcaniteExecutable() (for executables) or the DATA_DIR target property for libraries.
    /// @param highPriority if true, path is added to top of list. Else, it will have the lowest priority.
    static void addDataPath(const std::string &dataPath, bool highPriority = true);

  private:
    static std::filesystem::path findExecutablePath();

    /// list all possible data paths sorted from highest to lowest priority.
    static std::vector<std::filesystem::path> dataPaths;

    static bool useSourcePaths;
};

} // namespace vvv
