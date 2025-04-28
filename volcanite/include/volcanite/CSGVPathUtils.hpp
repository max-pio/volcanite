//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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

#include "vvv/util/Paths.hpp"

#include <filesystem>
#include <fmt/core.h>
#include <string>
#include <utility>

using namespace vvv;

namespace volcanite {

/** Helper function to remove the file extension from a file path, e.g. test.abc -> test.*/
static std::string stripFileExtension(const std::string &path) {
    return path.substr(0, path.find_last_of('.'));
}

[[maybe_unused]]
static std::filesystem::path expandPath(std::string path) {
    if (path.empty())
        return "";
    while (path.find('~') != std::string::npos)
        path.replace(path.find('~'), 1, Paths::getHomeDirectory().string());
    // make path absolute and canonical
    return absolute(std::filesystem::weakly_canonical(path)).make_preferred();
}

static std::string expandPathStr(std::string path) {
    return expandPath(std::move(path)).generic_string();
}

static std::string formatChunkPath(const std::string &formatted_path, int x, int y, int z) {
    return fmt::vformat(formatted_path, fmt::make_format_args(x, y, z));
}

static std::string combinedPathForAllChunks(const std::string &formatted_path, int max_file_index_xyz[3]) {
    if (max_file_index_xyz[0] == 0 && max_file_index_xyz[1] == 0 && max_file_index_xyz[2] == 0) {
        return fmt::vformat(formatted_path, fmt::make_format_args(max_file_index_xyz[0],
                                                                  max_file_index_xyz[1],
                                                                  max_file_index_xyz[2]));
    } else {
        std::string str_x = "0-" + std::to_string(max_file_index_xyz[0]);
        std::string str_y = "0-" + std::to_string(max_file_index_xyz[1]);
        std::string str_z = "0-" + std::to_string(max_file_index_xyz[2]);
        return fmt::vformat(formatted_path, fmt::make_format_args(str_x, str_y, str_z));
    }
}

} // namespace volcanite