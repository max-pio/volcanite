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

#include <complex>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <fmt/args.h>

using namespace vvv;

namespace volcanite {

fmt::dynamic_format_arg_store<fmt::format_context> create_fmt_args(const std::string &eval_name,
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

    fmt::dynamic_format_arg_store<fmt::format_context> fmt_args;
    fmt_args.push_back(fmt::arg("name", eval_name.empty() ? ("eval-" + time_stamp_ss.str()) : eval_name));
    fmt_args.push_back(fmt::arg("time", time_stamp_ss.str()));
    fmt_args.push_back(fmt::arg("args", args_ss.str()));
    // compression
    fmt_args.push_back(fmt::arg("comprate", comp_res.compression_rate));
    fmt_args.push_back(fmt::arg("comprate_pcnt", comp_res.compression_rate * 100.));
    fmt_args.push_back(fmt::arg("comp_s", comp_res.compression_total_seconds));
    fmt_args.push_back(fmt::arg("comp_mainpass_s", comp_res.compression_mainpass_seconds));
    fmt_args.push_back(fmt::arg("comp_prepass_s", comp_res.compression_prepass_seconds));
    fmt_args.push_back(fmt::arg("comp_gb_per_s", comp_res.compression_GB_per_s));
    fmt_args.push_back(fmt::arg("csgv_gb", comp_res.csgv_bytes * BYTE_TO_GB));
    fmt_args.push_back(fmt::arg("orig_gb", comp_res.original_volume_bytes * BYTE_TO_GB));
    fmt_args.push_back(fmt::arg("orig_bytes_per_voxel", comp_res.original_volume_bytes_per_voxel));
    fmt_args.push_back(fmt::arg("volume_dim", std::to_string(comp_res.volume_dim.x) + "x" + std::to_string(comp_res.volume_dim.y) + "x" + std::to_string(comp_res.volume_dim.z)));
    fmt_args.push_back(fmt::arg("volume_labels", comp_res.volume_labels));
    // decompression
    fmt_args.push_back(fmt::arg("decomp_cpu_gb_per_s", decomp_res.cpu_GB_per_s));
    fmt_args.push_back(fmt::arg("decomp_cpu_s", decomp_res.cpu_decoded_seconds));
    fmt_args.push_back(fmt::arg("decomp_gpu_gb_per_s", decomp_res.gpu_GB_per_s));
    fmt_args.push_back(fmt::arg("decomp_gpu_s", decomp_res.gpu_decoded_seconds));
    // rendering
    fmt_args.push_back(fmt::arg("min_spp", render_res.min_samples_per_pixel));
    fmt_args.push_back(fmt::arg("max_spp", render_res.max_samples_per_pixel));
    fmt_args.push_back(fmt::arg("frame_min_ms", render_res.frame_min_ms));
    fmt_args.push_back(fmt::arg("frame_avg_ms", render_res.frame_avg_ms));
    fmt_args.push_back(fmt::arg("frame_sdv_ms", render_res.frame_sdv_ms));
    fmt_args.push_back(fmt::arg("frame_med_ms", render_res.frame_med_ms));
    fmt_args.push_back(fmt::arg("frame_max_ms", render_res.frame_max_ms));
    for (int i = 0; i < 10; i++) {
        fmt_args.push_back(fmt::arg(("frame_ms_0" + std::to_string(i)).c_str(), render_res.frame_ms[i]));
        if (i < 6)
            fmt_args.push_back(fmt::arg(("frame_ms_1" + std::to_string(i)).c_str(), render_res.frame_ms[10 + i]));
    }
    fmt_args.push_back(fmt::arg("render_total_max", render_res.total_ms));
    fmt_args.push_back(fmt::arg("rendered_frames", render_res.accumulated_frames));
    fmt_args.push_back(fmt::arg("mem_framebuffer_mb", render_res.mem_framebuffers_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_uniformbuffer_mb", render_res.mem_ubos_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_materials_mb", render_res.mem_materials_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_encoding_Mb", render_res.mem_encoding_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_cache_mb", render_res.mem_cache_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_cache_used_mb", render_res.mem_cache_used_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_cache_fillrate", render_res.mem_cache_fill_rate));
    fmt_args.push_back(fmt::arg("mem_cache_fillrate_pcnt", render_res.mem_cache_fill_rate * 100.));
    fmt_args.push_back(fmt::arg("mem_emptyspace_mb", render_res.mem_empty_space_bytes * BYTE_TO_MB));
    fmt_args.push_back(fmt::arg("mem_total_mb", render_res.mem_total_bytes * BYTE_TO_MB));
    return std::move(fmt_args);
}

std::string EvaluationLogExport::format_evaluation_string(std::string format_string, const std::string &eval_name,
                                                          int argc, char *argv[],
                                                          CSGVCompressionEvaluationResults comp_res,
                                                          CSGVDecompressionEvaluationResults decomp_res,
                                                          CSGVRenderEvaluationResults render_res) {

    // replace all occurrences of all specifiers
    fmt::dynamic_format_arg_store<fmt::format_context> fmt_args = create_fmt_args(eval_name, argc, argv,
                                                                                  comp_res, decomp_res, render_res);
    try {
        return fmt::vformat(format_string, fmt_args);
    } catch (const fmt::format_error &err) {
        Logger(Error) << "evaluation output format error: " << format_string;
        throw;
    }
}

std::vector<std::string> EvaluationLogExport::get_all_evaluation_keys() {
    return {
        "name",
        "time",
        "args",
        "comprate",
        "comprate_pcnt",
        "comp_s",
        "comp_mainpass_s",
        "comp_prepass_s",
        "comp_gb_per_s",
        "csgv_gb",
        "orig_gb",
        "orig_bytes_per_voxel",
        "volume_dim",
        "volume_labels",
        "decomp_cpu_gb_per_s",
        "decomp_cpu_s",
        "decomp_gpu_gb_per_s",
        "decomp_gpu_s",
        "min_spp",
        "max_spp",
        "frame_min_ms",
        "frame_avg_ms",
        "frame_sdv_ms",
        "frame_med_ms",
        "frame_max_ms",
        "frame_ms_00",
        "frame_ms_01",
        "frame_ms_02",
        "frame_ms_03",
        "frame_ms_04",
        "frame_ms_05",
        "frame_ms_06",
        "frame_ms_07",
        "frame_ms_08",
        "frame_ms_09",
        "frame_ms_10",
        "frame_ms_11",
        "frame_ms_12",
        "frame_ms_13",
        "frame_ms_14",
        "frame_ms_15",
        "render_total_max",
        "rendered_frames",
        "mem_framebuffer_mb",
        "mem_uniformbuffer_mb",
        "mem_materials_mb",
        "mem_encoding_Mb",
        "mem_cache_mb",
        "mem_cache_used_mb",
        "mem_cache_fillrate",
        "mem_cache_fillrate_pcnt",
        "mem_emptyspace_mb",
        "mem_total_mb",
    };
}

int EvaluationLogExport::write_eval_logfile(const std::string &eval_logfile, const std::string &eval_name, int argc, char *argv[],
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
            Logger(Error) << "Could not open pre-existing evaluation log file " << eval_logfile;
            return 5;
        }
    }

    if (format_string.empty()) {
        // TODO: automatically create the default format and header from the replacement specifier vector
        // the default header and format string:
        std::stringstream header_ss;
        std::stringstream format_ss;
        header_ss << "# comment lines start with #\n";
        const auto &keys = get_all_evaluation_keys();
        for (int k = 0; k < keys.size(); k++) {
            header_ss << k;
            format_ss << "{" << k << "}";
            if (k < keys.size() - 1) {
                header_ss << ",";
                format_ss << ",";
            }
        }
        header_string = header_ss.str();
        format_string = format_ss.str();
    }
    std::ofstream output_file = std::ofstream(eval_logfile, std::ios_base::app);
    if (!output_file.is_open()) {
        Logger(Error) << "Could not open evaluation log file " << eval_logfile;
        return 5;
    }

    /* All first lines starting with #fmt: are concatenated into the format string
     *   #fmt:#title,time
     *   #fmt:{name},{time}
     * becomes:
     *   #title,time\n{name},{time}
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

} // namespace volcanite