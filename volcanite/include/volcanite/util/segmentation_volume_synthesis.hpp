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

#include "vvv/volren/Volume.hpp"

using namespace vvv;

namespace volcanite {

    constexpr unsigned char VSYNTH_PATH_PREFIX[] = "#synth";

    struct SyntheticSegmentationVolumeCfg {
        glm::uvec3 dim = {100, 100, 100};               /// dimensions of the volume in voxels
        glm::uvec3 min_region_dim = {1u, 1u, 1u};       /// target minimum size of each region
        glm::uvec3 max_region_dim = {32u, 32u, 32u};    /// target maximum size of each region
        unsigned long long seed = 4194968861ull;        /// random seed
        uint32_t voxels_per_label = 8192u;              /// smaller values increase the number of labels
        uint32_t max_label = ~0u;                       /// maximum possible label value
    };

    /// Creates a synthetic segmentation volume.
    /// The volume is created from a zero volume by inserting randomly sized axis-aligned boxes of random labels.
    /// Note that regions are randomly generated one after the other and overwrite previously set voxels, possibly
    /// resulting in final region sizes that are smaller than the minimum region dimension. It is possible that
    /// multiple regions have the same label.
    /// @returns a synthetically created segmentation volume
    std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(SyntheticSegmentationVolumeCfg cfg);


    /// Creates a synthetic segmentation volume based on the descriptor string.
    /// The volume is created from a zero volume by inserting randomly sized axis-aligned boxes of random labels.
    /// Note that regions are randomly generated one after the other and overwrite previously set voxels, possibly
    /// resulting in final region sizes that are smaller than the minimum region dimension. It is possible that
    /// multiple regions have the same label.
    /// The descriptor must follow this syntax:\n
    /// @code
    /// #synth_[args]
    /// @endcode{.cpp}
    /// where args is a _ separated list of the following keys:\n
    /// {W}x{H}x{D}  width, height, and depth of the volume in voxels\n
    /// l{VOXELS_PER_LABEL}  smaller values increase the number of labels in the volume\n
    /// max{MAXIMUM_LABEL}  maximum label value that will be assigned\n
    /// r{MIN_W}x{MIN_H}x{MIN_D}-{MAX_W}x{MAX_H}x{MAX_D} minimum and maximum sizes of the label regions\n
    /// @returns a synthetically created segmentation volume
    std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(std::string_view descr);

    /// @returns a segmentation volume where each voxel has a different label
    std::shared_ptr<Volume<uint32_t>> createWorstCaseSegmentationVolume(glm::uvec3 dim = {100, 100, 100});

}