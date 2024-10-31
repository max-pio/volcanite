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

#include <vvv/util/csv_utils.hpp>
#include <vvv/util/Logger.hpp>


std::vector<std::vector<float>> vvv::csv_float_import(const std::string& csv_path, std::vector<std::string>& column_names) {
    column_names.clear();
    std::ifstream csv(csv_path, std::ios::in);
    if (csv.is_open()) {
        // read column names from first row
        // column_names = ...
        // read

        csv.close();
        // return ..
    } else {
        Logger(ERROR) << "Could not open CSV file " << csv_path;
        return {};
    }
}

void vvv::csv_export(const std::vector<std::map<std::string, float>>& s, const std::string& path) {
    std::ofstream fout(path, std::ios::out);
    assert(fout.is_open());

    std::stringstream ss;
    std::vector<std::string> attributes;
    int i = 0;
    for(auto const& entry: s[0]) {
        attributes.push_back(entry.first);
        ss << entry.first;
        if(i++ < s[0].size()-1)
            ss << ",";
    }
    ss << "\n";
    fout << ss.str();
    for(const auto& m: s) {
        ss.str(std::string());
        for(i = 0; i < attributes.size(); i++) {
            float v = m.at(attributes[i]);
            if(v == std::floor(v))
                ss << std::to_string(static_cast<int>(v));
            else
                ss << std::to_string(v);
            if(i < attributes.size()-1)
                ss << ",";
        }
        ss << "\n";
        fout << ss.str();
    }

    fout.close();
}