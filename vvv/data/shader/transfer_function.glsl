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

#ifndef TRANSFER_FUNCTION_H
#define TRANSFER_FUNCTION_H

// integrate opacity along the ray segment. makes opacity independent of the raycast step size.
// There are two ways to think about this:
//
// - either your TF contains opacity in its alpha channel. In this case the value is within
//   [0,1] and we have to apply a opacity correction term that scales the distance used to compute the opacity `d'`
//   to the actual step size used during ray casting `d`. Setting `d'=1` we arrive at the following code for
//   the equations given in [].
//   ```glsl
//   float opacityUnitDistance = color.a; float stepSize = g_raycast.x;
//   float opacityCorrected = 1.0-pow(1.0-opacityUnitDistance, stepSize);
//   vec3 colorPremultipliedUnitDistance = color.rgb;
//   vec3 colorPremultipliedCorrected = color.rgb * (opacityCorrected/opacityUnitDistance);
//   ```
//
// - or, alternatively, your TF carries extinction coefficients in its alpha channel. The
//   extiniction coefficient is within [0,Infinity).
//   ```glsl
//   vec3 extinctionWeightedColor = color.rgb; float extinctionCoefficient = color.a;
//   float opacity = 1.0 - exp(-extinctionCoefficient*color.rgb);
//   vec3 colorOpacityPremultiplied = extinctionWeightedColor * (opacity / extinctionCoefficient);
//   ```
vec4 opacityCorrection(vec4 color, float stepSize) {
    // prevent division by zero
    if (color.a < 0.05f) return vec4(0, 0, 0, 0);

    float opacityUnitDistance = color.a;
    float opacityCorrected = 1.0-pow(1.0-opacityUnitDistance, stepSize);
    vec3 colorPremultipliedUnitDistance = color.rgb;
    vec3 colorPremultipliedCorrected = color.rgb * (opacityCorrected/opacityUnitDistance);
    return vec4(colorPremultipliedCorrected, opacityCorrected);
}

#endif /* TRANSFER_FUNCTION_H */