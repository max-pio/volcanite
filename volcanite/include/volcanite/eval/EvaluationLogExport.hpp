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

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "csgv_constants.incl"

namespace volcanite {

struct CSGVCompressionEvaluationResults {
    double compression_mainpass_seconds = 0.; ///< total compression time [s] without pre-pass and IO
    double compression_prepass_seconds = 0.;
    double compression_total_seconds = 0.;
    double compression_total_seconds_with_fileio = 0.;
    double csgv_base_encoding_bytes = 0.;
    double csgv_detail_encoding_bytes = 0.;
    double csgv_bytes = 0.;
    double compression_rate = -1.;
    double compression_GB_per_s = 0.;
    double original_volume_bytes = 0.;
    int original_volume_bytes_per_voxel = 0;
    glm::uvec3 volume_dim = {0u, 0u, 0u};
    uint32_t volume_labels = 0u;
    uint32_t brick_size = 0u;
    std::string operation_mask = {};
    std::string encoding_mode = {};
    bool random_access = false;
    bool detail_separation = false;
    uint32_t brick_labels_min = 0u;
    double brick_labels_avg = 0u;
    uint32_t brick_labels_max = 0u;
    uint32_t brick_palette_size_min = 0u;
    double brick_palette_size_avg = 0u;
    uint32_t brick_palette_size_max = 0u;
    uint32_t brick_palette_duplicates_min = 0u;
    double brick_palette_duplicates_avg = 0u;
    uint32_t brick_palette_duplicates_max = 0u;
    double brick_bytes_min = 0u;
    double brick_bytes_avg = 0u;
    double brick_bytes_max = 0u;
};

struct CSGVDecompressionEvaluationResults {
    double cpu_decoded_GB = 0.;
    double cpu_decoded_seconds = -1.;
    double cpu_GB_per_s = -1.;
    double gpu_decoded_GB = 0.;
    double gpu_decoded_seconds = -1.;
    double gpu_GB_per_s = -1.;
};

struct CSGVRenderEvaluationResults {
    double frame_min_ms = -1.;
    double frame_avg_ms = -1.;
    double frame_sdv_ms = -1.;
    double frame_med_ms = -1.;
    double frame_max_ms = -1.;
    double frame_ms[16] = {-1.};
    double total_ms = 0.;
    glm::dvec4 frame_gpu_min_ms = {-1.,-1.,-1.,-1.};
    glm::dvec4 frame_gpu_avg_ms = {-1.,-1.,-1.,-1.};
    glm::dvec4 frame_gpu_sdv_ms = {-1.,-1.,-1.,-1.};
    glm::dvec4 frame_gpu_med_ms = {-1.,-1.,-1.,-1.};
    glm::dvec4 frame_gpu_max_ms = {-1.,-1.,-1.,-1.};
    glm::dvec4 frame_gpu_ms[16] = {{-1.,-1.,-1.,-1.}};
    double mem_framebuffers_bytes = 0.;
    double mem_ubos_bytes = 0.;
    double mem_materials_bytes = 0.;
    double mem_encoding_bytes = 0.;
    double mem_cache_bytes = 0.;
    double mem_cache_used_bytes = 0.;
    double mem_cache_fill_rate = 0.;
    double mem_empty_space_bytes = 0.;
    double mem_total_bytes = 0.;
    uint32_t mem_cache_voxels_per_uint = 1; ///< how many label (indices) are stored per uint in the cache. If no cache paletting is used, this is 1.
    double mem_cache_packing_factor = 1.;   ///< effective packing factor in [1;8] <= voxels_per_uint (base elements must fit 8 voxels in an integer number of uints).
    int accumulated_frames = 0;
    int min_samples_per_pixel = 0;
    int max_samples_per_pixel = 0;
    double time_to_first_frame_s = -1.;
    //    double spp_min = 0.f;
    //    double spp_avg = 0.f;
    //    double spp_max = 0.f;
    //    double samples_total = 0.f;
};

class EvaluationLogExport {

  private:
    static std::string format_evaluation_string(std::string_view format_string, const std::string &eval_name,
                                                int argc, char *argv[],
                                                CSGVCompressionEvaluationResults comp_res,
                                                CSGVDecompressionEvaluationResults decomp_res,
                                                CSGVRenderEvaluationResults render_res);

  public:
    static std::vector<std::string> get_all_evaluation_keys();

    // TODO: should use std::expected instead of optional when moving to C++23

    /// Checks if all evaluation keys {k:[options]} are available.
    /// @returns empty on success, otherwise an error string describing the first detected error key.
    static std::optional<std::string> check_evaluation_string(std::string_view format_string);

    /// If the logfile exists: Checks if all specified evaluation keys {k:[options]} are available.
    /// @returns empty on success, otherwise an error string describing the first detected error key.
    static std::optional<std::string> check_eval_logfile(const std::string &eval_logfile);

    static int write_eval_logfile(const std::string &eval_logfile, const std::string &eval_name, int argc, char *argv[],
                                  CSGVCompressionEvaluationResults comp_res,
                                  CSGVDecompressionEvaluationResults decomp_res,
                                  CSGVRenderEvaluationResults render_res);
};

} // namespace volcanite
