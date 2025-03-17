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

#ifndef VOLCANITE_MATERIALS_GLSL
#define VOLCANITE_MATERIALS_GLSL

#include "volcanite/compression/csgv_utils.glsl"
#include "pcg_hash.glsl"

// Shading Materials ---------------------------------------------------------------------------------------------------
float getAttribute(uint label, uint attributeStart) {
    // attributeStart > valid buffer size means that we use the voxel label direclty (csgv_id)
    if(attributeStart == LABEL_AS_ATTRIBUTE)
        return float(label);
    else
        return g_attributes[attributeStart + label];
}

/// Returns the first material where the discriminator attribute falls into the discriminator interval, -1 for invisible
int getMaterial(uint label) {
#if CACHE_MODE == CACHE_VOXELS
    if (label == INVISIBLE_LABEL)
        return -1;
#endif
    for(int m = 0; m <= g_max_active_material; m++) {
        //#define HASHED_LABEL_VISIBILITY
//        #ifdef HASHED_LABEL_VISIBILITY
//            if (g_materials[m].discrAttributeStart == LABEL_AS_ATTRIBUTE) {
//                if ((label %  g_materials[m].discrIntervalMax) <  g_materials[m].discrIntervalMin)
//                    continue;
//                else
//                    return m;
//            }
//        #endif
        const float attr = getAttribute(label, g_materials[m].discrAttributeStart);
        if (attr >= g_materials[m].discrIntervalMin && attr <= g_materials[m].discrIntervalMax)
            return m;
    }
    // a material fits, return "invisible" material
    return -1;
}

bool isLabelVisible(uint label) {
#if CACHE_MODE == CACHE_VOXELS
    return label != INVISIBLE_LABEL && getMaterial(label) >= 0;
#else
    return getMaterial(label) >= 0;
#endif
}

vec4 getColor(uint label, int material) {
    assertf(material >= 0 && material <= g_max_active_material, "material %i assigned to label is invalid", material);
    assert(!any(isnan(vec4(g_materials[material].discrIntervalMin,  g_materials[material].discrIntervalMax,
                      g_materials[material].tfIntervalMin, g_materials[material].tfIntervalMax))),
           "NaN in shader attribute limits");

    // read attribute, map tfInterval to [0, 1]
    float v = 0.f;
    // wrapping mode: 0 = clamp (handled by textureLoD), 1 = repeat, 2 = random
    if (g_materials[material].wrapping == 2) { // repeat
        v = fract(float(hash_pcg2d(uvec2(label, floatBitsToUint(g_materials[material].tfIntervalMin))).x % 65536) / 65536.f
                  + g_materials[material].tfIntervalMax / 65536.f);
    }
    else {
        // clamp
        v = (getAttribute(label, g_materials[material].tfAttributeStart) - g_materials[material].tfIntervalMin)
            / (g_materials[material].tfIntervalMax + 1.f - g_materials[material].tfIntervalMin);

        // repeat
        if (g_materials[material].wrapping == 1) {
            const float interval_length = (g_materials[material].tfIntervalMax - g_materials[material].tfIntervalMin);
            v = fract(v) * (interval_length + 1.f) / interval_length;
        }
    }

    // problem that at least occurs on my old AMD RX480 card:
    // if the texture(..) call accesses a sampler from the array based on the variable m, the same sampler is selected
    // for all threads in a warp. See (non)-uniform control flow:
    // https://www.khronos.org/opengl/wiki/Sampler_(GLSL)#Non-uniform_flow_control
    // or  https://stackoverflow.com/questions/53734640/will-any-of-the-following-texture-lookups-cause-undefined-behavior-non-uniform
    //
    // This should not be an issue with GLSL version >= 4!
    // Anyways, here's a fix by "forcing" non-uniform control flow:
    if(material == 0)
        return vec4(textureLod(s_transferFunctions[0], v, 0.f).rgb, g_materials[0].opacity);
    return vec4(textureLod(s_transferFunctions[material], v, 0.f).rgb, g_materials[material].opacity);
}

#endif // VOLCANITE_MATERIALS_GLSL
