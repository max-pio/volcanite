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

#include <string>
#include <filesystem>

#include "vvv/util/Logger.hpp"

using namespace vvv;

namespace volcanite {

    /** Helper function to remove the file extension from a file path, e.g. test.abc -> test.*/
    static std::string stripFileExtension(std::string path) {
        return path.substr(0, path.find_last_of('.'));
    }

    static std::string expandPath(std::string path) {
        if(path.empty())
            return "";
        if(path.find('~') != std::string::npos)
            Logger(WARN) << "tilde-expansion is a bash specific feature. Use explicit home directory instead of '~' in " << path;
        // make path absolute and normalize
        std::filesystem::path absolute = std::filesystem::path(path);
        std::filesystem::path canonicalPath = std::filesystem::absolute(std::filesystem::weakly_canonical(absolute));
        return canonicalPath.make_preferred().string();
    }

    static std::string formatChunkPath(const std::string& formatted_path, int x, int y, int z) {
        std::string path = formatted_path;
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(x));
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(y));
        if (path.find_first_of("{}") != std::string::npos)
            path.replace(path.find_first_of("{}"), 2, std::to_string(z));
        return path;
    }

    static std::string combinedPathForAllChunks(const std::string& formatted_path, int max_file_index_xyz[3]) {
        if (max_file_index_xyz[0] == 0 && max_file_index_xyz[1] == 0 && max_file_index_xyz[2] == 0) {
            std::string path = formatted_path;
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0");
            return path;
        } else {
            std::string path = formatted_path;
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index_xyz[0] ));
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index_xyz[1] ));
            if (path.find_first_of("{}") != std::string::npos)
                path.replace(path.find_first_of("{}"), 2, "0-" + std::to_string(max_file_index_xyz[2] ));
            return path;
        }
    }

}