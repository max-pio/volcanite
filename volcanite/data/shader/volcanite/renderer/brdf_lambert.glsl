//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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

#ifndef VOLCANITE_BRDF_LAMBERT_GLSL
#define VOLCANITE_BRDF_LAMBERT_GLSL

#include "util.glsl"
#include "random.glsl"

// As a heads up on BRDF implementation, be referred to the "Crash Course in BRDF Implementation" by Jakub Boksansky
// https://boksajak.github.io/blog/BRDF where the fundamental principles are laid out. In particlar, we follow the
// proposed split into eval, sample, pdf, and eval_indirect functions. Some of the code is adapted from the CC0
// licensed brdf implementation header that accompanies the article.

// brdf_eval returns the fraction of reflected light  evaluates the geometry term as well.
vec3 brdf_eval(vec3 diffuseReflectance, vec3 normal, vec3 dir_in, vec3 dir_out) {
    return diffuseReflectance * ONE_OVER_PI * max(dot(normal, dir_out), 0.f);
}

vec3 brdf_sample(vec3 normal, vec2 u) {
    return sampleCosineWeightedHemisphereVoxel(u, normal);
}

float brdf_pdf(vec3 normal, vec3 dir_out) {
    return pdfCosineWeightedHemisphere(dir_out, normal);
}

/// samples a random outgoing reflection direction and updates the throughput.
/// returns true if the ray is reflected succesfully or false if it is consumed by the surface.
bool brdf_eval_indirect(vec3 diffuseReflectance, vec3 normal, vec3 dir_in, vec2 u, out vec3 dir_out, inout vec3 throughput) {
    // sample the geometry term for a new direction
    dir_out = sampleCosineWeightedHemisphereVoxel(u, normal);

    // apply surface albedo, geometry term and brdf, divided by PDF (terms cancel out)
    throughput *= diffuseReflectance;

    // if the sampled ray direction would be below the surface or if there is no contribution, end traversal
    return dot(normal, dir_out) * dot(throughput, vec3(1.f)) >= 0.f;
}

#endif // VOLDANITE_BRDF_LAMBERT_GLSL
