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
        char delimiter = ' ';
        std::vector<std::vector<float>> csv_file;

        std::string attribute;

        std::string first_line;
        std::getline(csv, first_line);
        first_line.erase(0, 1); // line starts with a '#'

        // extract attributes from first line
        std::stringstream ss(first_line);
        while (std::getline(ss, attribute, delimiter)) {
            column_names.emplace_back(attribute);
        }


        // read the values out of each line and insert them into the return map
        std::string line;
        std::vector<float> values;
        std::string val;
        while (std::getline(csv, line)) {
            std::stringstream sss(line);
            values.clear();
            for (auto & i : column_names) {
                std::getline(sss, val, delimiter);
                values.emplace_back(static_cast<float>(std::stold(val)));
            }
            csv_file.emplace_back(values);
        }
        csv.close();
	    return csv_file;
    } else {
        Logger(ERROR) << "Could not open CSV file " << csv_path;
        return {};
    }
}
