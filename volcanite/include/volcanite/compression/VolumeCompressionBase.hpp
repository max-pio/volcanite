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
#include <memory>
#include <unordered_map>
#include <vector>

#include "vvv/util/Logger.hpp"
#include "vvv/util/util.hpp"

#include "volcanite/compression/memory_mapping.hpp"

#define MULTIGRID_RECURSIVE_CONSTRUCTION

using namespace vvv;

namespace volcanite {

struct MultiGridNode {
    uint32_t label;
    bool constant_subregion;
};

class VolumeCompressionBase {

  public:
    virtual ~VolumeCompressionBase() = default;

    /**
     * Constructs a multigrid in out from finest to coarsest level for the given brick in the volume.
     * brick_dim must be a power of 2 but can reach to areas outside of the volume.
     * Levels are stored from finest (original) to coarsest (1³) resolution in out meaning that the first volume_dim^3
     * MultiGridNodes belong to the finest original brick level.
     * Entries for nodes in out lying completely outside the volume are set to 0xFFFFFFFF and are flagged as constant.
     * Nodes in the finest level L0 are always flagged as constant.
     * @param mark_constant_regions if false, none of the nodes are flagged as constant
     * @param set_out_of_bounds_to_parent if true, nodes out of the volume bounds are set to their parent label
     */
    static void constructMultiGrid(std::vector<MultiGridNode> &out, const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, const glm::uvec3 brick_start, const uint32_t brick_dim,
                                   bool mark_constant_regions, bool set_out_of_bounds_to_parent) {
        assert(!(brick_dim & (brick_dim - 1u)) && "brick_dim must be a power of 2");

        bool contains_voxels_outside_of_volume = glm::any(glm::greaterThanEqual(brick_start + brick_dim, volume_dim));

        out.resize(static_cast<size_t>(std::floor(brick_dim * brick_dim * brick_dim) * 8.f / 7.f));
        glm::uvec3 pos;
        // fill finest level with entries from volume
        for (pos.z = 0u; pos.z < brick_dim; pos.z++) {
            for (pos.y = 0u; pos.y < brick_dim; pos.y++) {
                for (pos.x = 0u; pos.x < brick_dim; pos.x++) {
                    if (glm::any(glm::greaterThanEqual(brick_start + pos, volume_dim))) {
                        out[voxel_pos2idx(pos, glm::uvec3(brick_dim))].label = 0xFFFFFFFF;
                    } else {
                        assert(volume[voxel_pos2idx(brick_start + pos, volume_dim)] != 0xFFFFFFFF && "Volume contains forbidden magic number to flag multigrid nodes lying outside the volume");
                        out[voxel_pos2idx(pos, glm::uvec3(brick_dim))].label = volume[voxel_pos2idx(brick_start + pos,
                                                                                                    volume_dim)];
                    }
                    // you can have different opinions about this but we set it to false because it leads to smaller numbers in our encoding -> better rANS compression:
                    out[voxel_pos2idx(pos, glm::uvec3(brick_dim))].constant_subregion = false;
                }
            }
        }

        // for all other levels: reduce 2x2x2 nodes form finer level to one node in current level
        // if all 8 finer nodes have constant subregions and the same label, flag this node as constant too.
        uint32_t prev_lod_start = 0u;
        uint32_t lod_start = brick_dim * brick_dim * brick_dim;
        for (uint32_t current_dim = brick_dim / 2u; current_dim >= 1u; current_dim /= 2u) {
            // iterate over all cells in finer level
            for (pos.z = 0u; pos.z < current_dim; pos.z++) {
                for (pos.y = 0u; pos.y < current_dim; pos.y++) {
                    for (pos.x = 0u; pos.x < current_dim; pos.x++) {
#ifdef MULTIGRID_RECURSIVE_CONSTRUCTION
                        // gather 8 child elements from finer level
                        MultiGridNode *child_elements[8];
                        glm::uvec3 delta;
                        int i = 0;
                        for (delta.z = 0u; delta.z <= 1u; delta.z++) {
                            for (delta.y = 0u; delta.y <= 1u; delta.y++) {
                                for (delta.x = 0u; delta.x <= 1u; delta.x++) {
                                    child_elements[i] = &out[prev_lod_start +
                                                             voxel_pos2idx((2u * pos) + delta, glm::uvec3(current_dim * 2u))];
                                    i++;
                                }
                            }
                        }
                        // find most frequent label in child elements and check if they're constant. This could be sped
                        // up, e.g. see https://towardsdatascience.com/countless-3d-vectorized-2x-downsampling-of-labeled-volume-images-using-python-and-numpy-59d686c2f75
                        // but is not a performance bottleneck at the moment.
                        uint32_t max_ocurrences = 0u;
                        uint32_t max_label = 0xFFFFFFFF;
                        bool constant = mark_constant_regions;
                        for (i = 0; i < 8; i++) {
                            // skip children lying completely outside the volume
                            // these will be marked as constant and will be assigned to the most frequent label
                            if (child_elements[i]->label == 0xFFFFFFFF) {
                                continue;
                            }

                            if (max_label != child_elements[i]->label) {
                                // all children must have constant subregions (or be a single element) and the same label for this node to also be constant
                                if (max_label != 0xFFFFFFFF)
                                    constant = false;

                                uint32_t occurrences = 1u;
                                for (int n = i + 1; n < 8; n++) {
                                    if (child_elements[n]->label == child_elements[i]->label)
                                        occurrences++;
                                }
                                if (occurrences > max_ocurrences) {
                                    max_label = child_elements[i]->label;
                                    max_ocurrences = occurrences;
                                }
                            }

                            // all children must have constant subregions (or be a single element) and the same label for this node to also be constant
                            constant = constant && (prev_lod_start == 0u || child_elements[i]->constant_subregion); // && (child_elements[i]->label == child_elements[0]->label);
                        }
#else
                        // deprecated computation: use most frequent labels from finest instead of last LOD
                        glm::uvec3 volume_pos = brick_start + pos * (brick_dim / current_dim);
                        uint32_t max_label;
                        bool constant;
                        if (glm::any(glm::greaterThanEqual(volume_pos, volume_dim))) {
                            max_label = 0xFFFFFFFF;
                            constant = true;
                        } else {
                            max_label = maxOccurrenceInBrick(volume, volume_dim, volume_pos, glm::uvec3(brick_dim / current_dim));
                            constant = isHomogeneousBrick(volume, volume_dim, volume_pos, glm::uvec3(brick_dim / current_dim));
                        }
#endif

                        out[lod_start + voxel_pos2idx(pos, glm::uvec3(current_dim))].label = max_label;
                        assert(current_dim > 1u || max_label != 0xFFFFFFFF && "Setting invalid label 0xFFFFFFFF to coarsest brick node");
                        out[lod_start + voxel_pos2idx(pos, glm::uvec3(current_dim))].constant_subregion = constant;
                        assert(lod_start + voxel_pos2idx(pos, glm::uvec3(current_dim)) < out.size() && "Writing multigrid node outside of array");
                    }
                }
            }
            prev_lod_start = lod_start;
            lod_start += current_dim * current_dim * current_dim;
        }

        assert(lod_start == out.size() && "Allocated too much memory for Multigrid nodes");

        // if parts of the brick are outside of the volume, assign those grid nodes the most freq. label
        if (contains_voxels_outside_of_volume && set_out_of_bounds_to_parent) {
            // iterate from second coarsest to finest level, assign invalids to parent
            prev_lod_start = out.size() - 1u;

            for (uint32_t current_dim = 2; current_dim <= brick_dim; current_dim *= 2u) {

                lod_start = prev_lod_start - current_dim * current_dim * current_dim;

                assert(lod_start < out.size() && "current lod start out of bounds");
                assert(prev_lod_start < out.size() && "parent lod start out of bounds");

                // iterate over all cells in finer level
                for (pos.z = 0u; pos.z < current_dim; pos.z++) {
                    for (pos.y = 0u; pos.y < current_dim; pos.y++) {
                        for (pos.x = 0u; pos.x < current_dim; pos.x++) {

                            const auto &parent_element = out.at(prev_lod_start + voxel_pos2idx(pos / 2u, glm::uvec3(current_dim / 2u)));
                            auto &cur_element = out.at(lod_start + voxel_pos2idx(pos, glm::uvec3(current_dim)));
                            if (cur_element.label == 0xFFFFFFFF) {
                                cur_element.label = parent_element.label;
                            }
                        }
                    }
                }

                prev_lod_start = lod_start;
            }
        }
    }

    /**
     * Compresses the volume and stores the encoded representation as object attributes.
     */
    virtual void compress(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, bool verbose = false) = 0;

    /**
     * Decompresses the encoded volume from this object's attribute to an uncompressed volume.
     * @return
     */
    virtual std::shared_ptr<std::vector<uint32_t>> decompress() const = 0;

    /**
     * @return Compression ratio achieved after calling compress.
     */
    virtual float getCompressionRatio() const { return -1.f; }

    /**
     * Returns the entry value with the maximum occurrence in the given brick.
     */
    static uint32_t maxOccurrenceInBrick(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, const glm::uvec3 brick_start, const glm::uvec3 brick_dim) {
        if (brick_dim.x == 1 && brick_dim.y == 1 && brick_dim.z == 1)
            return volume[voxel_pos2idx(brick_start, volume_dim)];

        // count all occurring items in a hash map
        std::unordered_map<uint32_t, int> occurences;
        glm::uvec3 pos;
        for (pos.z = 0u; pos.z < brick_dim.z; pos.z++) {
            for (pos.y = 0u; pos.y < brick_dim.y; pos.y++) {
                for (pos.x = 0u; pos.x < brick_dim.x; pos.x++) {
                    if (glm::any(glm::greaterThanEqual(brick_start + pos, volume_dim)))
                        continue;

                    uint32_t v = volume[voxel_pos2idx(brick_start + pos, volume_dim)];
                    if (occurences.contains(v))
                        occurences[v]++;
                    else
                        occurences[v] = 1;
                }
            }
        }
        // find max occurrence
        uint32_t max_occurring_id = 1337;
        int max_occurrence = -1;
        for (const auto &n : occurences) {
            if (n.second > max_occurrence) {
                max_occurrence = n.second;
                max_occurring_id = n.first;
            }
        }

        assert(max_occurrence > 0 && "Couldn't count any values in volume. Did you pass an empty brick query (= volume zero)?");
        return max_occurring_id;
    }

    static bool isHomogeneousBrick(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, const glm::uvec3 brick_start, const glm::uvec3 brick_dim) {
        if ((brick_dim.x == 1 && brick_dim.y == 1 && brick_dim.z == 1) || glm::any(glm::greaterThanEqual(brick_start, volume_dim)))
            return true;

        const uint32_t v = volume[voxel_pos2idx(brick_start, volume_dim)];
        glm::uvec3 pos;
        for (pos.z = 0u; pos.z < brick_dim.z; pos.z++) {
            for (pos.y = 0u; pos.y < brick_dim.y; pos.y++) {
                for (pos.x = 0u; pos.x < brick_dim.x; pos.x++) {
                    if (glm::any(glm::greaterThanEqual(brick_start + pos, volume_dim)))
                        continue;

                    if (volume[voxel_pos2idx(brick_start + pos, volume_dim)] != v)
                        return false;
                }
            }
        }
        return true;
    }

    /**
     * Compresses and decompresses the given volume, then checks for all differences.
     * @return true if output and input are the same, false if there are (de)compression errors.
     */
    virtual bool test(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, const bool compress_first = false) {
        assert(volume.size() == volume_dim.x * volume_dim.y * volume_dim.z && "volume size does not match dimension");

        Logger(Info) << "Running compression test ------------------------------------";
        MiniTimer timer;
        if (compress_first) {
            Logger(Info) << "Encode";
            compress(volume, volume_dim);
            Logger(Info) << " finished in " << timer.restart() << "s with compression ratio " << getCompressionRatio() << "%";
        }
        Logger(Info) << "Decode";
        const std::shared_ptr<std::vector<uint32_t>> out = decompress();
        Logger(Info) << " finished in " << timer.elapsed() << "s";

        if (volume.size() != out->size()) {
            Logger(Error) << "Compressed in and out sizes don't match";
            Logger(Info) << "-------------------------------------------------------------";
            return false;
        }

        static constexpr int max_error_lines = 32;
        size_t error_count = 0;
        for (size_t i = 0; i < volume.size(); i++) {
            if (volume[i] != (*out)[i]) {
                if (error_count < max_error_lines)
                    Logger(Error) << "error at " << str(voxel_idx2pos(i, volume_dim)) << " in " << volume[i] << " != out " << (*out)[i];
                else if (error_count == max_error_lines)
                    Logger(Error) << "[...] skipping additional errors";
                error_count++;
            }
        }

        Logger(Info) << "finished with " << error_count << " errors (" << (100.f * static_cast<float>(error_count) / static_cast<float>(volume.size())) << "%)";
        Logger(Info) << "-------------------------------------------------------------";
        return error_count == 0;
    }
};

} // namespace volcanite