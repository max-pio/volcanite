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

#include <cassert>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "vvv/util/Logger.hpp"

namespace vvv {

/// Imports a comma ',' separated CSV file that contains only numerical values as data points and returns a vector
/// containing the value list of each row as float numbers. The first CSV row is assumed to contain the column names.
/// @param column_names a vector into which the column names of the CSV file will be written
/// @return a vector where the i-th element contains the list of values in the i-th CSV file row
std::vector<std::vector<float>> csv_float_import(const std::string& csv_path, std::vector<std::string>& column_names);

void csv_export(const std::vector<std::map<std::string, float>>& s, const std::string& path);


} // namespace vvv
