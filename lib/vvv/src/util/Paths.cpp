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

#include <vvv/util/Paths.hpp>

#include <optional>
#include <vvv/util/Logger.hpp>

#ifdef _WIN32
#include <Windows.h>
#include <array>
#include <process.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace vvv {

std::vector<std::filesystem::path> Paths::dataPaths;
bool Paths::useSourcePaths;

std::vector<std::string> split(const std::string &s, char delimiter) {
    // method from range-v3, released under the Boost Software License
    // https://github.com/ericniebler/range-v3/blob/master/test/view/split.cpp
    // released under the Boost Software License 1.0

    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void Paths::initPaths(const std::string &dataPathsStr) {
    auto executableDataPath = findExecutablePath() / "data";
    useSourcePaths = !std::filesystem::exists(executableDataPath);
    Logger(Debug) << (useSourcePaths ? "no data/ directory found in binary directory. Use source paths." : "use data/ directory relative to binary.");

    if (useSourcePaths) {
        auto paths = split(dataPathsStr, ';');
        // iterate in reverse: dataPaths is sorted from lowest to highest priority. We want highest to lowest priority.
        for (int i = paths.size() - 1; i >= 0; i--)
            dataPaths.emplace_back(paths[i]);
    } else {
        dataPaths.push_back(executableDataPath);
    }

    for (auto &path : dataPaths)
        Logger(Debug) << "data path: " << path.string();
}

void Paths::addDataPath(const std::string &dataPath, bool highPriority) {
    // only add if we use source paths. for binary paths, this logic needs to be handled within CMake.
    if (!useSourcePaths)
        return;

    auto path = std::filesystem::path(dataPath);
    if (!std::filesystem::exists(path))
        throw std::runtime_error("data directory " + path.string() + " does not exist.");

    if (highPriority)
        dataPaths.insert(dataPaths.begin(), {path});
    else
        dataPaths.push_back(path);
    Logger(Debug) << "data path: " << path.string();
}

std::filesystem::path Paths::findExecutablePath() {
    // src for defines: https://stackoverflow.com/a/8249232/13565664
    // src for the method: https://stackoverflow.com/a/1528493/13565664
#if defined(unix) || defined(__unix) || defined(__unix__)
    auto path = std::filesystem::canonical("/proc/self/exe").remove_filename();
#elif defined(_WIN32)
    std::array<wchar_t, 1024> buffer{};
    DWORD ret = GetModuleFileNameW(NULL, buffer.data(), buffer.size());
    if (ret == buffer.size())
        throw std::runtime_error("Executable path does not fit in buffer. Consider increasing its size.");
    if (ret == 0)
        throw std::runtime_error("Finding the path of the executable failed.");
    auto path = std::filesystem::path(std::wstring(buffer.data())).remove_filename();
#else
#error Paths::findExecutablePath() not implemented on this platform
#endif
    return path;
}

std::filesystem::path Paths::findDataPath(const std::string &path) {
    for (auto &dataDir : dataPaths) {
        auto absolutePath = dataDir / path;
        if (std::filesystem::exists(absolutePath))
            return absolutePath;
    }

    std::stringstream ss;
    ss << "file " << path << " not found in data/ directories.";
    for (auto &dataDir : dataPaths)
        ss << "\nsearched in: " << dataDir.string();
    Logger(Error) << ss.str();
    throw std::runtime_error("Data path '" + path + "' not found.");
}

bool Paths::hasDataPath(const std::string &path) {
    for (auto &dataDir : dataPaths) {
        auto absolutePath = dataDir / path;
        if (std::filesystem::exists(absolutePath))
            return true;
    }
    return false;
}

std::vector<std::filesystem::path> Paths::getDataDirectories() {
    return dataPaths;
}

std::filesystem::path Paths::findShaderPath(const std::string &filename) {
    return findDataPath((std::filesystem::path("shader") / filename).string());
}

std::vector<std::filesystem::path> Paths::getShaderDirectories() {
    static std::optional<std::vector<std::filesystem::path>> dirs;

    if (!dirs.has_value()) {
        dirs = std::vector<std::filesystem::path>{};
        for (auto &dataDir : dataPaths) {
            auto shaderDir = dataDir / "shader";
            if (std::filesystem::exists(shaderDir))
                dirs->push_back(shaderDir);
        }

        for (auto &shaderDir : dirs.value())
            Logger(Debug) << "shader include path: " << shaderDir.string();
    }

    return dirs.value();
}

std::filesystem::path Paths::getTempFileWithName(const std::string &name) {
    create_directory(std::filesystem::temp_directory_path() / "volcanite");
#ifdef _WIN32
    return std::filesystem::temp_directory_path() / "volcanite" / (std::to_string(_getpid()) + "_" + name);
#else
    return std::filesystem::temp_directory_path() / "volcanite" / (std::to_string(getpid()) + "_" + name);
#endif
}

std::filesystem::path Paths::getTempFileForDataPath(const std::filesystem::path &dataPath) {
    std::string filename;
    for (auto &e : std::filesystem::relative(dataPath, Paths::getDataDirectories().back()).relative_path())
        if (e.string() != "..")
            filename += "_" + e.string();
    filename = filename.substr(1); // remove first '_'

    return getTempFileWithName(filename);
}

std::filesystem::path Paths::getLocalFileForDataPath(const std::filesystem::path &dataPath) {
    std::string filename;
    for (auto &e : std::filesystem::relative(dataPath, Paths::getDataDirectories().back()).relative_path())
        if (e.string() != "..")
            filename += "_" + e.string();
    filename = filename.substr(1); // remove first '_'

    return filename;
}

std::filesystem::path Paths::getHomeDirectory() {
#ifdef _WIN32
    std::string drive = getenv("HOMEDRIVE");
    std::string path = getenv("HOMEPATH");
    if (drive.empty() || path.empty())
        return {getenv("USERPROFILE")};
    else
        return {drive.append(path)};
#else
    struct passwd *pwd = getpwuid(getuid());
    if (pwd)
        return {pwd->pw_dir};
    else
        return {getenv("HOME")};
#endif
}

} // namespace vvv
