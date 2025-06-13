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

#include "vvv/util/Logger.hpp"

namespace vvv {

void try_ffmpeg_video_encoding(const std::string& video_fmt_file_in, int frame_rate, std::string video_output_file) {

    // TODO: use a direct C++ API for video encoding instead of ffmpeg system calls

    // TODO: include frame times with video_timing:
    // system("ffmpeg -f concat -safe 0 -i ./volcanite_video/video_timing.txt ./volcanite_video/video.mp4");

    const std::string prefix = video_fmt_file_in.substr(0, video_fmt_file_in.find('{'));
    const std::string files = prefix + "*" + video_fmt_file_in.substr(video_fmt_file_in.rfind('}') + 1);
    if (video_output_file.empty())
        video_output_file = prefix + ".mp4";
    const std::string ffmpeg_cmd = "ffmpeg -loglevel quiet -n -framerate " + std::to_string(frame_rate) + " -pattern_type glob -i '" + files + "' -c:v libx264 -pix_fmt yuv420p " + video_output_file;
    if (system(ffmpeg_cmd.c_str()) != 0)
        Logger(Warn) << "System call failed: " << ffmpeg_cmd;
    else
        Logger(Debug) << "finished " << ffmpeg_cmd;
}

void try_ffmpeg_video_encoding_with_timing(const std::string& frame_timing_file, const std::string &video_output_file) {

    // TODO: use a direct C++ API for video encoding instead of ffmpeg system calls

    const std::string ffmpeg_cmd = "ffmpeg -loglevel quiet -f concat -safe 0 -i " + frame_timing_file + " " + video_output_file;
    if (system(ffmpeg_cmd.c_str()) != 0)
        Logger(Warn) << "System call failed: " << ffmpeg_cmd;
    else
        Logger(Debug) << "finished " << ffmpeg_cmd;
}


}