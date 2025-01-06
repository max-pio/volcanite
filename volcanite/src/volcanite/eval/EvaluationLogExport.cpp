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

#include "volcanite/eval/EvaluationLogExport.hpp"
#include "vvv/util/Logger.hpp"

#include <ctime>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <fmt/include/fmt/core.h>

using namespace vvv;

namespace volcanite {

std::string dtos(double v, int decimal_places=3) {
    v = std::round(v * pow(10., decimal_places)) / pow(10., decimal_places);
    std::string s = std::to_string(v);
    auto decpos = s.rfind('.');
    int skipped_chars = 0;
    if (decpos != std::string::npos) {
        auto lpos = s.end() - 1;
        while (*(lpos - skipped_chars) == '0')
            skipped_chars++;
        if (*(lpos - skipped_chars) == '.')
            skipped_chars++;
    }
    return s.substr(0, s.size() - skipped_chars);
}

std::string EvaluationLogExport::format_evaluation_string(std::string format_string, const std::string& eval_name,
                                                             int argc, char *argv[],
                                                             CSGVCompressionEvaluationResults comp_res,
                                                             CSGVDecompressionEvaluationResults decomp_res,
                                                             CSGVRenderEvaluationResults render_res) {
    // obtain time stamp
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::stringstream time_stamp_ss;
    time_stamp_ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    // obtain args string
    std::stringstream args_ss;
    for (int i = 0; i < argc; i++) {
        args_ss << argv[i];
        if (i < argc - 1)
            args_ss << " "; // if arguments should be comma separated, this would be ","
    }

    // the list of replacement specifiers and the replacement values:
    std::vector<fmt::arg> replace_str = {
            {"name", eval_name.empty() ? ("eval-" + time_stamp_ss.str()) : eval_name},
            {"time", time_stamp_ss.str()},
            {"args", args_ss.str()},
            // compression
            {"comprate", dtos(comp_res.compression_rate)},
            {"comprate_pcnt", dtos(comp_res.compression_rate * 100.)},
            {"comp_s", dtos(comp_res.compression_total_seconds)},
            {"comp_mainpass_s", dtos(comp_res.compression_mainpass_seconds)},
            {"comp_prepass_s", dtos(comp_res.compression_prepass_seconds)},
            {"comp_gb_per_s", dtos(comp_res.compression_GB_per_s)},
            {"csgv_gb", dtos(comp_res.csgv_bytes * BYTE_TO_GB)},
            {"orig_gb", dtos(comp_res.original_volume_bytes * BYTE_TO_GB)},
            {"orig_bytes_per_voxel", dtos(comp_res.original_volume_bytes * BYTE_TO_GB)},
            {"volume_dim", std::to_string(comp_res.volume_dim.x) + "x" + std::to_string(comp_res.volume_dim.y) + "x"
                            + std::to_string(comp_res.volume_dim.z)},
            {"volume_labels", comp_res.volume_labels)},
            // decompression
            {"decomp_cpu_gb_per_s", dtos(decomp_res.cpu_GB_per_s)},
            {"decomp_gpu_gb_per_s", dtos(decomp_res.gpu_GB_per_s)},
            // rendering
            {"frame_min_ms", dtos(render_res.frame_min_ms)},
            {"frame_avg_ms", dtos(render_res.frame_avg_ms)},
            {"frame_sdv_ms", dtos(render_res.frame_sdv_ms)},
            {"frame_med_ms", dtos(render_res.frame_med_ms)},
            {"frame_max_ms", dtos(render_res.frame_max_ms)},
            {"render_total_ms", dtos(render_res.total_ms)},
            {"mem_framebuffer_mb", dtos(render_res.mem_framebuffers_bytes * BYTE_TO_MB)},
            {"mem_uniformbuffer_mb", dtos(render_res.mem_ubos_bytes * BYTE_TO_MB)},
            {"mem_materials_mb", dtos(render_res.mem_materials_bytes * BYTE_TO_MB)},
            {"mem_encoding_mb", dtos(render_res.mem_encoding_bytes * BYTE_TO_MB)},
            {"mem_cache_mb", dtos(render_res.mem_cache_bytes * BYTE_TO_MB)},
            {"mem_emptyspace_mb", dtos(render_res.mem_empty_space_bytes * BYTE_TO_MB)},
            {"mem_total_mb", dtos(render_res.mem_total_bytes * BYTE_TO_MB)},
            {"rendered_frames", dtos(render_res.accumulated_frames)},
    };
    for (int i = 0; i < 10; i++) {
        replace_str.emplace_back("frame_ms_0" + std::to_string(i), dtos(render_res.frame_ms[i]));
        if (i < 6)
            replace_str.emplace_back("frame_ms_1" + std::to_string(i), dtos(render_res.frame_ms[10 + i]));
    }

    // replace all occurrences of all specifiers
    bool replaced;
    do {
        replaced = false;
        for (const auto& r: replace_str) {
            auto replace_key = "%" + r.first;
            auto pos = format_string.find(replace_key);
            if (pos != std::string::npos) {
                format_string.replace(pos, replace_key.size(), r.second);
                replaced = true;
            }
        }
    } while (replaced);
    return format_string;
}

int EvaluationLogExport::write_eval_logfile(const std::string& eval_logfile, const std::string& eval_name, int argc, char *argv[],
                       CSGVCompressionEvaluationResults comp_res,
                       CSGVDecompressionEvaluationResults decomp_res,
                       CSGVRenderEvaluationResults render_res) {
    bool logfile_exists = std::filesystem::exists(eval_logfile);
    std::string format_string;
    std::string header_string;
    if (logfile_exists) {
        std::ifstream file = std::ifstream(eval_logfile);
        if (file.is_open()) {
            std::string line;
            std::getline(file, line);
            while (line.starts_with("#fmt:")) {
                line = line.substr(5);
                format_string += (line + "\n");
                std::getline(file, line);
            }
            if (format_string.ends_with('\n'))
                format_string.pop_back(); // remove trailing '\n'
            file.close();
        } else {
            Logger(ERROR) << "Could not open pre-existing evaluation log file " << eval_logfile;
            return 5;
        }
    }

    if (format_string.empty()) {
        // TODO: automatically create the default format and header from the replacement specifier vector
        // the default header and format string:
        header_string = "# to remove the comment lines from the file use: sed -i '/#/d' ./" + eval_logfile + "\n"
                        "name,volume_dim,orig_GB,csgv_GB,CR_%,comp_s,comp_GB_per_s,"
                        "decomp_cpu_GB_per_s,decomp_gpu_GB_per_s,frame_min_ms,frame_avg_ms,frame_sdv_ms,frame_med_ms,frame_max_ms,"
                        "render_total_ms,mem_framebuffer_MB,mem_uniformbuffer_MB,mem_materials_MB,mem_encoding_MB,"
                        "mem_cache_MB,mem_emptyspace_MB,mem_total_MB,render_frames,"
                        "frame_ms_00,frame_ms_01,frame_ms_02,frame_ms_03,frame_ms_04,frame_ms_05,frame_ms_06,"
                        "frame_ms_07,frame_ms_08,frame_ms_09,frame_ms_10,frame_ms_11,frame_ms_12,frame_ms_13,"
                        "frame_ms_14,frame_ms_15";
        format_string = "#%time [%args] result:\n"
                        "%name,%volume_dim,%orig_GB,%csgv_GB,%cr,%comp_s,%comp_GB_per_s,%"
                        "decomp_cpu_GB_per_s,%decomp_gpu_GB_per_s,%frame_min_ms,%frame_avg_ms,%frame_sdv_ms,%frame_med_ms,%frame_max_ms,%"
                        "render_total_ms,%mem_framebuffer_MB,%mem_uniformbuffer_MB,%mem_materials_MB,%mem_encoding_MB,%"
                        "mem_cache_MB,%mem_emptyspace_MB,%mem_total_MB,%render_frames,"
                        "%frame_ms_00,%frame_ms_01,%frame_ms_02,%frame_ms_03,%frame_ms_04,%frame_ms_05,%frame_ms_06,"
                        "%frame_ms_07,%frame_ms_08,%frame_ms_09,%frame_ms_10,%frame_ms_11,%frame_ms_12,%frame_ms_13,"
                        "%frame_ms_14,%frame_ms_15";
    }
    std::ofstream output_file = std::ofstream(eval_logfile, std::ios_base::app);
    if (!output_file.is_open()) {
        Logger(ERROR) << "Could not open evaluation log file " << eval_logfile;
        return 5;
    }

    /* All first lines starting with #fmt: are concatenated into the format string
     *   #fmt:#title,time
     *   #fmt:%name,%time
     * becomes:
     *   #title,time\n%name,%time
     * which will be written out as:
     *   #title,time
     *   my_name,XX-XX-XXTXX:XX:XX
     */

    // write out the current format string if this is a new file
    if (!logfile_exists) {
        // start all newlines in the format string with "#fmt:"
        std::string out_fmt_string = format_string;
        int pos = 0;
        while (true) {
            pos = out_fmt_string.find('\n', pos);
            if (pos == std::string::npos)
                break;
            out_fmt_string.replace(pos, 1, "\n#fmt:");
            pos += 6;
        }
        output_file << "#fmt:" << out_fmt_string << std::endl;
        // add the header string that is not part of the format string
        if (!header_string.empty())
            output_file << header_string << std::endl;
    }
    // replace all replacement specifiers in the format string
    output_file << format_evaluation_string(format_string, eval_name, argc, argv, comp_res, decomp_res, render_res)
                << std::endl;
    output_file.close();
    return 0;
}


}