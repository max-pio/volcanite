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

#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CSGVDatabase.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

using namespace vvv;

namespace volcanite {

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_NOT_SUPPORTED = 2;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;
constexpr int RET_IO_ERROR = 5;

/// Parses the argc command line arguments in argv to fill the VolcaniteArgs struct and to load, compress, or create
/// the specified CSGV compressed segmentation volume and its attribute database. This is a helper method for different
/// executable entry points of Volcanite.
/// @param args the parsed and updated VolcaniteArgs
/// @param compressedSegmentationVolume CSGV object if loading/importing/creating was successful, nullptr otherwise
/// @param csgvDatabase CSGV attribute database object if creation was successful, nullptr otherwise
/// @param argc number of command line arguments in argv
/// @param argv command line arguments
/// @returns RET_SUCCESS on success or an error code otherwise
int volcanite_provide_args_and_csgv(VolcaniteArgs &args,
                                    std::shared_ptr<CompressedSegmentationVolume> &compressedSegmentationVolume,
                                    std::shared_ptr<CSGVDatabase> &csgvDatabase,
                                    int argc, char *argv[]);

} // namespace volcanite