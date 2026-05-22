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
#include <string>
#include <vector>

namespace vvv {

/// Performs a system call to the ffmpeg command to encode the input frame files file_names_{frame_index}.type into a video file.
/// If ffmpeg system calls are not available, the function will have no effect.
/// @param video_fmt_file_in path template to input image files with * placeholder for the frame index
/// @param frame_rate output video frame rate
/// @param video_output_file output video file
void try_ffmpeg_video_encoding(const std::string& video_fmt_file_in, int frame_rate = 30, std::string video_output_file = "");

/// Performs a system call to the ffmpeg command to encode the input frame files file_names_{frame_index}.type into a video file.
/// If ffmpeg system calls are not available, the function will have no effect.
/// @param frame_timing_file must contain an ffmpeg compatible frame image file and timing list
/// @param video_output_file output video file
void try_ffmpeg_video_encoding_with_timing(const std::string& frame_timing_file, const std::string &video_output_file);

/// Performs a system call to the ffmpeg command to encode the input frame files file_names_{frame_index}.type into a video file.
/// If ffmpeg system calls are not available, the function will have no effect.
/// @param video_fmt_file_in path template to input image files with * placeholder for the frame index
/// @param frame_times_ms a vector of render times per frame in [ms] for all rendered frames in order
/// @param video_output_file output video file
void try_ffmpeg_video_encoding_with_timing(const std::string& video_fmt_file_in, const std::vector<float> & frame_times_ms, std::string video_output_file = "");

}
