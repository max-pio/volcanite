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


void csv_export(const std::vector<std::map<std::string, float>>& s, const std::string& path) {
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



} // namespace vvv
