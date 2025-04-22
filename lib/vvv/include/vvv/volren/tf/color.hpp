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
#include <cmath>
#include <cstddef>
#include <vector>

template <typename T>
inline void premultiplyAlpha(T *data, size_t dataCount, float scale = std::numeric_limits<T>::max()) {
    for (size_t pixel = 0; pixel < dataCount; pixel += 4) {
        const auto alpha = static_cast<float>(data[pixel + 3]) / scale;
        data[pixel + 0] = std::round(static_cast<float>(data[pixel + 0]) * alpha);
        data[pixel + 1] = std::round(static_cast<float>(data[pixel + 1]) * alpha);
        data[pixel + 2] = std::round(static_cast<float>(data[pixel + 2]) * alpha);
    }
}

template <typename T>
inline void premultiplyAlpha01(T *data, size_t dataCount) {
    for (size_t pixel = 0; pixel < dataCount; pixel += 4) {
        const auto alpha = data[pixel + 3];
        data[pixel + 0] *= alpha;
        data[pixel + 1] *= alpha;
        data[pixel + 2] *= alpha;
    }
}

template <typename T>
inline void premultiplyAlpha(std::vector<T> &data) { premultiplyAlpha(data.data(), data.size()); }
template <typename T>
inline void premultiplyAlpha01(std::vector<T> &data) { premultiplyAlpha01(data.data(), data.size()); }
