//  Copyright (C) 2024, Max Piochowiak and Fabian Schiekel, Karlsruhe Institute of Technology
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

#include <glm/glm.hpp>

namespace vvv {
//  ------------------------------------------------------------------------------
//  COLOR SPACE

// colorspace conversion
// Illuminant: D65,  Observer: 2°
const float xn = 0.9505f;
const float yn = 1.f;
const float zn = 1.089f;

static const glm::vec3 RGBToXYZ(glm::vec3 rgb) {
    auto conversion = [](float c) {
        if (c > 0.04045)
            c = glm::pow(((c + 0.055) / 1.055), 2.4f);
        else
            c = c / 12.92f;
        return c;
    };
    float x = conversion(rgb.r);
    float y = conversion(rgb.g);
    float z = conversion(rgb.b);

    const glm::mat3 mat(0.4124f, 0.2126f, 0.0193f,
                        0.3576f, 0.7152f, 0.1192f,
                        0.1805f, 0.0722f, 0.9505);
    return mat * glm::vec3(x, y, z);
}

static const glm::vec3 XYZToLAB(glm::vec3 xyz) {
    auto f = [](float c) {
        const float delta = 0.008856f; // glm::pow(6.f / 29.f, 3);
        if (c > delta)
            return glm::pow(c, 1.f / 3.f);
        else
            return 7.787f * c + 4.f / 29.f;
    };
    const float x = f(xyz.x / xn);
    const float y = f(xyz.y / yn);
    const float z = f(xyz.z / zn);

    return glm::vec3(116.f * y - 16.f,
                     500.f * (x - y),
                     200.f * (y - z));
}

static const glm::vec3 RGBToLAB(glm::vec3 rgb) {
    return XYZToLAB(RGBToXYZ(rgb));
}

static const glm::vec3 LabToMsh(glm::vec3 lab) {
    const float M = glm::sqrt(glm::pow(lab.x, 2.f) + glm::pow(lab.y, 2.f) + glm::pow(lab.z, 2.f));
    const float s = glm::acos(lab.x / M);
    const float h = glm::atan(lab.z, lab.y);

    return glm::vec3(M, s, h);
}

static const glm::vec3 RGBToMsh(glm::vec3 rgb) {
    return LabToMsh(RGBToLAB(rgb));
}

static const float adjustHue(glm::vec3 msh, float unsatM) {
    // adjust hue in msh color space based on https://www.kennethmoreland.com/color-maps/
    if (msh.x >= unsatM) {
        return msh.z;
    } else {
        float hSpin = msh.y * glm::sqrt(glm::pow(unsatM, 2.f) - glm::pow(msh.x, 2.f)) /
                      (msh.x * glm::sin(msh.y));
        if (msh.z > -M_PI / 3.f)
            return msh.z + hSpin;
        else
            return msh.z - hSpin;
    }
}

static const glm::vec3 XYZToRGB(glm::vec3 xyz) {
    auto conversion = [](float c) {
        if (c > 0.0031308)
            return 1.055f * glm::pow(c, 1.f / 2.4f) - 0.055f;
        else
            return c * 12.92f;
    };
    float r = xyz.x * 3.2406 + xyz.y * -1.5372 + xyz.z * -0.4986;
    float g = xyz.x * -0.9689 + xyz.y * 1.8758 + xyz.z * 0.0415;
    float b = xyz.x * 0.0557 + xyz.y * -0.2040 + xyz.z * 1.0570;
    r = conversion(r);
    g = conversion(g);
    b = conversion(b);

    // clipping to [0,1]
    float maxVal = glm::max(glm::max(r, g), b);
    if (maxVal > 1.f) {
        r /= maxVal;
        g /= maxVal;
        b /= maxVal;
    }

    if (r < 0)
        r = 0;
    if (g < 0)
        g = 0;
    if (b < 0)
        b = 0;

    return glm::vec3(r, g, b);
}

static const glm::vec3 LABToXYZ(glm::vec3 lab) {
    auto finv = [](float c) {
        const float delta = 0.008856f; // glm::pow(6.f / 29.f, 3);
        if (float temp = glm::pow(c, 3.f); temp > delta)
            return temp;
        else
            return (c - 16.f / 116.f) / 7.787f;
    };
    return glm::vec3(xn * finv(((lab.x + 16.f) / 116.f) + lab.y / 500.f),
                     yn * finv((lab.x + 16.f) / 116.f),
                     zn * finv(((lab.x + 16.f) / 116.f) - lab.z / 200.f));
    ;
}

static const glm::vec3 LABToRGB(glm::vec3 lab) {
    return XYZToRGB(LABToXYZ(lab));
}

static const glm::vec3 MshToLAB(glm::vec3 msh) {
    const float L = msh.x * glm::cos(msh.y);
    const float a = msh.x * glm::sin(msh.y) * glm::cos(msh.z);
    const float b = msh.x * glm::sin(msh.y) * glm::sin(msh.z);
    return glm::vec3(L, a, b);
}

static const glm::vec3 MshToRGB(glm::vec3 msh) {
    return LABToRGB(MshToLAB(msh));
}
} // namespace vvv