#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

template <typename T> inline void premultiplyAlpha(T *data, size_t dataCount, float scale = std::numeric_limits<T>::max()) {
    for (size_t pixel = 0; pixel < dataCount; pixel += 4) {
        const auto alpha = static_cast<float>(data[pixel + 3]) / scale;
        data[pixel + 0] = std::round(static_cast<float>(data[pixel + 0]) * alpha);
        data[pixel + 1] = std::round(static_cast<float>(data[pixel + 1]) * alpha);
        data[pixel + 2] = std::round(static_cast<float>(data[pixel + 2]) * alpha);
    }
}

template <typename T> inline void premultiplyAlpha01(T *data, size_t dataCount) {
    for (size_t pixel = 0; pixel < dataCount; pixel += 4) {
        const auto alpha = data[pixel + 3];
        data[pixel + 0] *= alpha;
        data[pixel + 1] *= alpha;
        data[pixel + 2] *= alpha;
    }
}

template <typename T> inline void premultiplyAlpha(std::vector<T> &data) { premultiplyAlpha(data.data(), data.size()); }
template <typename T> inline void premultiplyAlpha01(std::vector<T> &data) { premultiplyAlpha01(data.data(), data.size()); }
