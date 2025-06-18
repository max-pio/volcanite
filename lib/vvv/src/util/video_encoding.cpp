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

#include "vvv/util/video_encoding.hpp"

#include "fmt/format.h"
#include "vvv/util/Logger.hpp"
#include "vvv/util/Paths.hpp"

#include <fstream>

namespace vvv {

void try_ffmpeg_video_encoding(const std::string& video_fmt_file_in, int frame_rate, std::string video_output_file) {
    // TODO: use a direct C++ API for video encoding instead of ffmpeg system calls

    const std::string prefix = video_fmt_file_in.substr(0, video_fmt_file_in.find('{'));
    const std::string files = prefix + "*" + video_fmt_file_in.substr(video_fmt_file_in.rfind('}') + 1);
    if (video_output_file.empty())
        video_output_file = (prefix.empty() ? "out" : prefix) + ".mp4";
    const std::string ffmpeg_cmd = "ffmpeg -y -loglevel quiet -framerate " + std::to_string(frame_rate) + " -pattern_type glob -i '" + files + "' -c:v libx264 -pix_fmt yuv420p " + video_output_file;
    Logger(Debug) << "Encoding video " << ffmpeg_cmd;
    if (system(ffmpeg_cmd.c_str()) != 0)
        Logger(Warn) << "System call failed: " << ffmpeg_cmd;
}

void try_ffmpeg_video_encoding_with_timing(const std::string& frame_timing_file, const std::string &video_output_file) {
    // TODO: use a direct C++ API for video encoding instead of ffmpeg system calls

    const std::string ffmpeg_cmd = "ffmpeg -y -loglevel quiet -f concat -safe 0 -i " + frame_timing_file + " " + video_output_file;
    Logger(Debug) << "Encoding video " << ffmpeg_cmd;
    if (system(ffmpeg_cmd.c_str()) != 0)
        Logger(Warn) << "System call failed: " << ffmpeg_cmd;
}

void try_ffmpeg_video_encoding_with_timing(const std::string& video_fmt_file_in, const std::vector<float>& frame_times_ms, std::string video_output_file) {
    const std::string prefix = video_fmt_file_in.substr(0, video_fmt_file_in.find('{'));
    if (video_output_file.empty())
        video_output_file = (prefix.empty() ? "out" : prefix) + ".mp4";

    const std::filesystem::path frame_timing_file = Paths::getTempFileWithName("video_timing_file.txt");
    if (std::ofstream timing_out(frame_timing_file);
        timing_out.is_open()) {
        for (int frame_idx = 0; frame_idx < frame_times_ms.size(); frame_idx++) {
            std::string video_file_path = fmt::vformat(video_fmt_file_in, fmt::make_format_args(frame_idx));
            timing_out << "file '" << video_file_path << "'\n";
            timing_out << "duration " << (static_cast<double>(frame_times_ms[frame_idx])/1000.) << "\n";
        }
        timing_out.close();
    } else {
        Logger(Warn) << "Could not create frame timing file " << frame_timing_file << " for ffmpeg video encoding";
    }

    try_ffmpeg_video_encoding_with_timing(frame_timing_file.generic_string(), video_output_file);
}


}