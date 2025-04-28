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

#include "csgv_constants.incl"

using namespace vvv;

namespace volcanite {

struct SyntheticSegmentationVolumeCfg {
    glm::uvec3 dim = {100, 100, 100};            /// dimensions of the volume in voxels
    glm::uvec3 min_region_dim = {10u, 10u, 10u}; /// target minimum size of each region
    glm::uvec3 max_region_dim = {50u, 50u, 50u}; /// target maximum size of each region
    float sphere_box_shape = 0.5f;               /// how sphere- (0) or box-shaped regions are, in [0, 1]
    unsigned long long seed = 4194968861ull;     /// random seed
    uint32_t voxels_per_label = 8192u;           /// smaller values increase the number of labels
    uint32_t max_label = ~0u;                    /// maximum possible label value
};

/// Creates a synthetic segmentation volume.
/// The volume is created from a zero volume by inserting randomly sized axis-aligned boxes of random labels.
/// Note that regions are randomly generated one after the other and overwrite previously set voxels, possibly
/// resulting in final region sizes that are smaller than the minimum region dimension. It is possible that
/// multiple regions have the same label.
/// @returns a synthetically created segmentation volume
std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(SyntheticSegmentationVolumeCfg cfg);

static constexpr const char *getDummySegmentationVolumeHelpStr() {
    return "        " CSGV_SYNTH_PREFIX_STR
           "[_arg]* with arg in\n"
           "          d[x]x[y]x[z]: volume dimension [x,y,z]\n"
           "          l[v]: voxels per label [v] (higher values produce fewer labels)\n"
           "          max[v]: maximum label value [v]\n"
           "          r[a]x[b]x[c]-[s]x[t]x[u]: target label region size min. [a,b,c], max. [s,t,u]\n"
           "          b[v]: region shape control: [v]=0 all spheres, [v]=1 all boxes, 0<[v]<1 a mix of both\n"
           "          s[v]: deterministic random seed [v]. for chunked data, set to s{}[v]{}[v]{}";
}

/// Creates a synthetic segmentation volume based on the descriptor string.
/// The volume is created from a zero volume by inserting randomly sized axis-aligned boxes of random labels.
/// Note that regions are randomly generated one after the other and overwrite previously set voxels, possibly
/// resulting in final region sizes that are smaller than the minimum region dimension. It is possible that
/// multiple regions have the same label.
/// The descriptor must follow this syntax:\n
/// @code
/// +synth_[args]
/// @endcode{.cpp}
/// where args is a _ separated list of the following keys:\n
/// d{W}x{H}x{D}  width, height, and depth of the volume in voxels\n
/// l{VOXELS_PER_LABEL}  smaller values increase the number of labels in the volume\n
/// max{MAXIMUM_LABEL}  maximum label value that will be assigned\n
/// r{MIN_W}x{MIN_H}x{MIN_D}-{MAX_W}x{MAX_H}x{MAX_D} minimum and maximum sizes of the label regions\n
/// b{[0-1]}  value in [0,1]. 0: only spherical region shapes, 1: only box shapes, 0.5: a mix of the two.
/// s{seed} seed to initialize the deterministic random number generation
/// @returns a synthetically created segmentation volume
std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(std::string_view descr);

/// @returns a segmentation volume where each voxel has a different label
std::shared_ptr<Volume<uint32_t>> createWorstCaseSegmentationVolume(glm::uvec3 dim = {100, 100, 100});

} // namespace volcanite