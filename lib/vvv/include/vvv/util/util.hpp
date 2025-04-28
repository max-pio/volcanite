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

#include <chrono>
#include <iomanip>
#include <random>

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

namespace vvv {

// string stuff ------------------------------------------------------------------

static const std::string leading_zeros_string(int id, int digits) {
    std::stringstream ss;
    ss << std::setw(digits) << std::setfill('0') << id;
    return ss.str();
}

//  ------------------------------------------------------------------------------
//  VULKAN

static uint32_t roundUp(uint32_t numToRound, uint32_t multiple) {
    assert(multiple);
    return ((numToRound + multiple - 1) / multiple) * multiple;
}

static uint32_t roundUpPowerOfTwo(uint32_t numToRound, uint32_t multiple) {
    assert(multiple && ((multiple & (multiple - 1)) == 0));
    return (numToRound + multiple - 1) & -multiple;
}

static vk::Extent3D getDispatchSize(vk::Extent2D extent, vk::Extent2D workgroupSize) {
    return vk::Extent3D(roundUpPowerOfTwo(extent.width, workgroupSize.width) / workgroupSize.width, roundUpPowerOfTwo(extent.height, workgroupSize.height) / workgroupSize.height, 1);
}

static vk::Extent3D getDispatchSize(vk::Extent3D extent, vk::Extent3D workgroupSize) {
    return vk::Extent3D(roundUpPowerOfTwo(extent.width, workgroupSize.width) / workgroupSize.width, roundUpPowerOfTwo(extent.height, workgroupSize.height) / workgroupSize.height,
                        roundUpPowerOfTwo(extent.depth, workgroupSize.depth) / workgroupSize.depth);
}

static vk::Extent3D getDispatchSize(uint32_t width, uint32_t height, uint32_t depth, vk::Extent3D workgroupSize) {
    return vk::Extent3D(roundUpPowerOfTwo(width, workgroupSize.width) / workgroupSize.width, roundUpPowerOfTwo(height, workgroupSize.height) / workgroupSize.height,
                        roundUpPowerOfTwo(depth, workgroupSize.depth) / workgroupSize.depth);
}

//  ------------------------------------------------------------------------------
//  GLSL

glm::mat4 removeTranslation(glm::mat4 mat);

template <typename T>
size_t vectorByteSize(const typename std::vector<T> &vec) { return sizeof(T) * vec.size(); }

/// Constructs a string representation of an array with n elements.
template <typename T>
std::string array_string(const T *v, const size_t n) {
    std::stringstream out;
    out << "{";
    for (size_t i = 0; i < n; i++) {
        if (i > 0)
            out << ", ";
        out << v[i];
    }
    out << "}";
    return out.str();
}

std::string str(glm::vec2 v);
std::string str(glm::vec3 v);
std::string str(glm::vec4 v);
std::string str(glm::ivec2 v);
std::string str(glm::ivec3 v);
std::string str(glm::ivec4 v);
std::string str(glm::uvec2 v);
std::string str(glm::uvec3 v);
std::string str(glm::uvec4 v);
std::string str(glm::mat3 v);
std::string str(glm::mat4 v);

/// Converts cartesian coordinates to spherical coordinates.\n
/// spherical components: (0 <= theta <= pi, -pi <= phi <= pi, r >= 0)\n
/// cartesian: z axis points upwards
glm::vec3 spherical2cartesian(const glm::vec3 &v);

/// Converts cartesian coordinates to spherical coordinates.
/// spherical components: (0 <= theta <= pi, -pi <= phi <= pi, r >= 0)\n
/// cartesian: z axis points upwards, w is 1
glm::vec4 spherical2cartesian(const glm::vec4 &v);

/// Converts spherical coordinates to cartesian coordinates.
/// spherical components: (0 <= theta <= pi, -pi <= phi <= pi, r >= 0)\n
/// cartesian: z axis points upwards
glm::vec3 cartesian2spherical(const glm::vec3 &v);

/// Converts spherical coordinates to cartesian coordinates.
/// spherical components: (0 <= theta <= pi, -pi <= phi <= pi, r >= 0)\n
/// cartesian: z axis points upwards, w is 1
glm::vec4 cartesian2spherical(const glm::vec4 &v);

//  ------------------------------------------------------------------------------
//  STATISTICS

/// Computs a histogram of the vector values with the given bin number. If interpolate is true, values contribute proportionally to their adjacent two bins when they're discretized to the bins.
/// @param values vector to compute the histogram on
/// @param bins number of histogram bins
/// @param interpolate if the discretization of values to bins is interpolated
/// @param min value in values that is mapped to the histogram start
/// @param max value in values that is mapped to the histogram end
/// @return a newly created vector with the histogram counts
std::vector<float> computeHistogram(const std::vector<float> &values, int bins, bool interpolate, float min, float max);

//  ------------------------------------------------------------------------------
//  TIMING

/// @brief Lightweight (but inaccurate) timer class for measuring elapsed time in seconds using std::chrono::high_resolution_clock. Usage:
///
/// MiniTimer t;\n
/// // do stuff..\n
/// auto seconds_since_creation = t.elapsed();
class MiniTimer {
  public:
    MiniTimer() : m_startTime(std::chrono::high_resolution_clock::now()) {}
    ~MiniTimer() = default;

    /// Restarts the timer.
    /// @return the time in seconds passed since the object was created or since the last time start was called.
    double restart() {
        auto ret = elapsed();
        m_startTime = std::chrono::high_resolution_clock::now();
        return ret;
    }

    /// @return the time in seconds passed since the object was created or since the last time start was called.
    inline double elapsed() { return static_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - m_startTime).count(); }

    static float getFloatSystemClock() {
        return static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()) / 1000.f;
    }

    static std::string getCurrentDateTime(const std::string &format = "%Y-%m-%d %X") {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), format.c_str());
        return ss.str();
    }

  private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
};

void logLibraryAvailabilty();

} // namespace vvv
