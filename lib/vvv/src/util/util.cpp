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

#include "vvv/util/util.hpp"
#include "vvv/util/Logger.hpp"

#include <glm/gtc/constants.hpp>

#include <unordered_map>

glm::mat4 vvv::removeTranslation(glm::mat4 mat) {
    mat[3][0] = mat[3][1] = mat[3][2] = 0.f;
    return mat;
}

std::string vvv::str(glm::vec2 v) { return array_string(&v.x, 2); }
std::string vvv::str(glm::vec3 v) { return array_string(&v.x, 3); }
std::string vvv::str(glm::vec4 v) { return array_string(&v.x, 4); }
std::string vvv::str(glm::ivec2 v) { return array_string(&v.x, 2); }
std::string vvv::str(glm::ivec3 v) { return array_string(&v.x, 3); }
std::string vvv::str(glm::ivec4 v) { return array_string(&v.x, 4); }
std::string vvv::str(glm::uvec2 v) { return array_string(&v.x, 2); }
std::string vvv::str(glm::uvec3 v) { return array_string(&v.x, 3); }
std::string vvv::str(glm::uvec4 v) { return array_string(&v.x, 4); }
std::string vvv::str(glm::mat3 v) { return "[" + array_string(&v[0].x, 3) + " | " + array_string(&v[1].x, 3) + " | " + array_string(&v[2].x, 3) + "]"; }
std::string vvv::str(glm::mat4 v) { return "[" + array_string(&v[0].x, 4) + " | " + array_string(&v[1].x, 4) + " | " + array_string(&v[2].x, 4) + " | " + array_string(&v[3].x, 4) + "]"; }

glm::vec3 vvv::spherical2cartesian(const glm::vec3 &v) {
    return {v.z * glm::cos(v.y) * glm::sin(v.x),
            v.z * glm::sin(v.y) * glm::sin(v.x),
            v.z * glm::cos(v.x)};
}

glm::vec4 vvv::spherical2cartesian(const glm::vec4 &v) {
    return {vvv::spherical2cartesian(static_cast<const glm::vec3>(v)), 1.f};
}

glm::vec3 vvv::cartesian2spherical(const glm::vec3 &v) {
    float r = glm::length(v);
    if (r > 0)
        return {glm::acos(v.z / r), glm::atan(v.y, v.x), r};
    else
        return {0.f, 0.f, 0.f};
}

glm::vec4 vvv::cartesian2spherical(const glm::vec4 &v) {
    return {vvv::cartesian2spherical(static_cast<const glm::vec3>(v)), 1.f};
}

std::vector<float> vvv::computeHistogram(const std::vector<float> &values, int bins, bool interpolate, float min, float max) {
    std::vector<float> histogram(bins, 0.f);
    if (min >= max) {
        Logger(Error) << "min must be smaller than max when computing a histogram. Returning zero.";
        return histogram;
    }

    // could parallelize histogram computation with OpenMP and custom "sum" reduction for histogram array
    // #pragma omp parallel for default(none) shared(values, histogram, bins, min, max)
    for (int i = 0; i < values.size(); i++) {
        if (interpolate) {
            double intpart;
            double pos = (values[i] - min) / (max - min) * static_cast<float>(bins);
            auto fractional = static_cast<float>(modf(pos, &intpart));
            histogram[std::clamp(static_cast<int>(std::floor(pos)), 0, bins - 1)] += (1.f - fractional);
            histogram[std::clamp(static_cast<int>(std::ceil(pos)), 0, bins - 1)] += fractional;
        } else {
            histogram[std::clamp(static_cast<int>((values[i] - min) / (max - min) * static_cast<float>(bins)), 0, bins - 1)]++;
        }
    }

    return histogram;
}

void vvv::logLibraryAvailabilty() {
    if (system(":") >= 0)
        vvv::Logger(vvv::Debug) << "System calls available.";
    else
        vvv::Logger(vvv::Warn) << "System calls not available.";

#ifdef _OPENMP
    {
        std::unordered_map<unsigned, std::string> ver{{200505, "2.5"}, {200805, "3.0"}, {201107, "3.1"}, {201307, "4.0"}, {201511, "4.5"}, {201811, "5.0"}, {202011, "5.1"}};
        if (ver.contains(_OPENMP))
            vvv::Logger(vvv::Debug) << "OpenMP " + ver.at(_OPENMP) + " available.";
        else
            vvv::Logger(vvv::Debug) << "OpenMP " + std::to_string(_OPENMP) + " available.";
    }
#else
    vvv::Logger(vvv::WARN) << "OpenMP not available.";
#endif

#ifdef LIB_HIGHFIVE
    vvv::Logger(vvv::Debug) << "HDF5 library available.";
#else
    vvv::Logger(vvv::WARN) << "HDF5 library not available.";
#endif
}
