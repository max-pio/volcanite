//  Copyright (C) 2024, Max Piochowiak, Reiner Dolp and Fabian Schiekel, Karlsruhe Institute of Technology
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

/// Imports CSV file separated by the attribute_csv_separator that contains only numerical values as data points and returns a vector
/// containing the value list of each row as float numbers. The first CSV row is assumed to contain the column names.
/// @param attribute_csv_separator the CSV file attribute seperator
/// @param column_names a vector into which the column names of the CSV file will be written
/// @return a vector where the i-th element contains the list of values in the i-th CSV file row
std::vector<std::vector<float>> csv_float_import(const std::string &csv_path, const std::string &attribute_csv_separator, std::vector<std::string> &column_names);

std::vector<unsigned int> csv_label_column_import(const std::string &csv_path, const std::string &attribute_csv_separator, std::string &label_column);

template <typename T>
void csv_export(const std::vector<std::map<std::string, T>> &s, const std::string &path) {
    std::ofstream fout(path, std::ios::out);
    assert(fout.is_open());

    std::stringstream ss;
    std::vector<std::string> attributes;
    int i = 0;
    for (auto const &entry : s[0]) {
        attributes.push_back(entry.first);
        ss << entry.first;
        if (i++ < s[0].size() - 1)
            ss << ",";
    }
    ss << "\n";
    fout << ss.str();
    for (const auto &m : s) {
        ss.str(std::string());
        for (i = 0; i < attributes.size(); i++) {
            T v = m.at(attributes[i]);
            if (std::is_floating_point<T>() && v == std::floor(v))
                ss << std::to_string(static_cast<long long>(v));
            else
                ss << v;
            if (i < attributes.size() - 1)
                ss << ",";
        }
        ss << "\n";
        fout << ss.str();
    }

    fout.close();
}

} // namespace vvv
