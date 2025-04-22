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

#include <vvv/util/Logger.hpp>
#include <vvv/util/csv_utils.hpp>

std::vector<std::vector<float>> vvv::csv_float_import(const std::string &csv_path, const std::string &attribute_csv_separator, std::vector<std::string> &column_names) {
    column_names.clear();
    std::ifstream csv(csv_path, std::ios::in);
    if (csv.is_open()) {
        std::vector<std::vector<float>> csv_file;

        std::string attribute;

        std::string first_line;
        std::getline(csv, first_line);
        if (first_line.front() == '#')
            first_line.erase(0, 1); // line starts with a '#'

        // extract attributes from first line
        size_t pos = 0;
        while ((pos = first_line.find(attribute_csv_separator)) != std::string::npos || pos == std::string::npos && !first_line.empty()) {
            if (pos == std::string::npos)
                pos = first_line.size();
            attribute = first_line.substr(0, pos);
            column_names.emplace_back(attribute);
            first_line.erase(0, pos + attribute_csv_separator.length());
        }

        // read the values out of each line and insert them into the return map
        std::string line;
        std::vector<float> values;
        std::string val;
        while (std::getline(csv, line)) {
            values.clear();
            while ((pos = line.find(attribute_csv_separator)) != std::string::npos || pos == std::string::npos && !line.empty()) {
                if (pos == std::string::npos)
                    pos = line.size();
                val = line.substr(0, pos);
                values.emplace_back(static_cast<float>(std::stold(val)));
                line.erase(0, pos + attribute_csv_separator.length());
            }
            csv_file.emplace_back(values);
        }
        csv.close();
        return csv_file;
    } else {
        Logger(Error) << "Could not open CSV file " << csv_path;
        return {};
    }
}

std::vector<unsigned int> vvv::csv_label_column_import(const std::string &csv_path, const std::string &attribute_csv_separator, std::string &label_column) {
    std::ifstream csv(csv_path, std::ios::in);
    if (csv.is_open()) {
        std::vector<unsigned int> label_column_values;

        std::string attribute;

        std::string first_line;
        std::getline(csv, first_line);
        if (first_line.front() == '#')
            first_line.erase(0, 1); // line starts with a '#'

        // extract index of label_column column
        int label_column_idx = -1, count_rows = 0;
        size_t pos = 0;
        while ((pos = first_line.find(attribute_csv_separator)) != std::string::npos || pos == std::string::npos && !first_line.empty()) {
            if (pos == std::string::npos)
                pos = first_line.size();
            attribute = first_line.substr(0, pos);
            if (attribute == label_column)
                label_column_idx = count_rows;
            count_rows++;
            first_line.erase(0, pos + attribute_csv_separator.length());
        }

        // read only the label_column values and ignore the rest
        std::string line;
        unsigned int value = 0;
        std::string val;
        while (std::getline(csv, line)) {
            value = 0;
            for (int i = 0; i < count_rows; i++) {
                pos = line.find(attribute_csv_separator);
                val = line.substr(0, pos);
                if (i == label_column_idx)
                    value = std::stoul(val);
                line.erase(0, pos + attribute_csv_separator.length());
            }
            label_column_values.emplace_back(value);
        }
        csv.close();
        return label_column_values;
    } else {
        Logger(Error) << "Could not open CSV file " << csv_path;
        return {};
    }
}
