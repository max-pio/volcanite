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

#ifndef CSGV_UTILS_GLSL
#define CSGV_UTILS_GLSL

#include "cpp_glsl_include/csgv_constants.h"

#ifdef NDEBUG
    #define assert(X, S)
    #define assertf(X, S, P)
#else
    #define assert(X, S) if(!(X)) debugPrintfEXT(S)
    #define assertf(X, S, P) if(!(X)) debugPrintfEXT(S, P)
#endif

// Memory Access and Indexing Utilities --------------------------------------------------------------------------------
uint brick_pos2idx(const uvec3 brick_idx, const uvec3 brick_count) {
    return brick_idx.x + brick_count.x * (brick_idx.y + brick_count.y * brick_idx.z);
}

// adds the element offset (one unit = 4 byte) to the 64 bit address represented in an uvec2
uvec2 bufferAddressAdd(uvec2 address, const uint uint_elem_offset) {
    uint carry;
    // the offset is measured in uints but we have to add 4 byte per uint. To prevent uint overflow, we repeat the op:
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    return address;
}

// substracts the element offset (one unit = 4 byte) from the 64 bit address represented in an uvec2
uvec2 bufferAddressSub(uvec2 address, const uint uint_elem_offset) {
    uint borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    return address;
}

uint getBrickStart(uint brick_idx) {
    if(g_brick_starts[brick_idx] > g_brick_starts[brick_idx + 1u])
        return 0u;
    else
        return g_brick_starts[brick_idx];
}

uint getBrickEnd(uint brick_idx) {
    return g_brick_starts[brick_idx + 1u];
}

uint getBrickEncodingLength(uint brick_idx) {
    return getBrickEnd(brick_idx) - getBrickStart(brick_idx);
}

EncodingRef getBrickEncodingRef(uint brick_idx) {
    return EncodingRef(bufferAddressAdd(g_encoding_buffer_addresses[brick_idx / g_brick_idx_to_enc_vector], getBrickStart(brick_idx)));
}

uint getPaletteSizeHeaderIndex() {
#ifdef SEPARATE_DETAIL
    return 2u * g_lod_count - 1u;
#else
    return 2u * g_lod_count;
#endif
}

uint getBrickPaletteLength(uint brick_idx) {
    return getBrickEncodingRef(brick_idx).buf[getPaletteSizeHeaderIndex()];
}

#ifdef SEPARATE_DETAIL
EncodingRef getBrickDetailEncodingRef(uint brick_idx) {
    return EncodingRef(bufferAddressAdd(g_detail_buffer_address, g_detail_starts[brick_idx]));
}
#endif

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
    for(int m = 0; m <= g_max_active_material; m++) {
        if(getAttribute(label, g_materials[m].discrAttributeStart) >= g_materials[m].discrIntervalMin
        && getAttribute(label, g_materials[m].discrAttributeStart) <= g_materials[m].discrIntervalMax)
        return m;
    }
    // a material fits, return "invisible" material
    return -1;
}

bool isLabelVisible(uint label) {
    return getMaterial(label) >= 0;
}

vec4 getColor(uint label, int material) {
    assert(material >= 0 && material <= g_max_active_material, "invalid material request in getColor");

    // read attribute, map tfInterval to [0, 1]
    float v = (getAttribute(label, g_materials[material].tfAttributeStart) - g_materials[material].tfIntervalMin)
                / (g_materials[material].tfIntervalMax + 1.f - g_materials[material].tfIntervalMin);
    // wrapping mode: 0 = clamp, handled by textureLoD, 1 = repeat
    if(g_materials[material].wrapping == 1) { // repeat
        float interval_length = (g_materials[material].tfIntervalMax - g_materials[material].tfIntervalMin);
        // ToDo:  visible for small tfMax - tfMin differences
        v = fract(v) * (interval_length + 1.f) / interval_length;
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

    assertf(material >= 0 && material <= g_max_active_material, "material %i assigned to label is invalid", material);
    assert(!any(isnan(vec4(g_materials[material].discrIntervalMin,  g_materials[material].discrIntervalMax,
                           g_materials[material].tfIntervalMin, g_materials[material].tfIntervalMax))),
           "NaN in shader attribute limits");
    return vec4(textureLod(s_transferFunctions[material], v, 0.f).rgb, g_materials[material].opacity);
}

// Background Color ----------------------------------------------------------------------------------------------------

/// The background is a tilted interpolation between two colors g_background_color_a and g_background_color_b
vec4 get_background_color(vec2 fragCoord) {
    float bgWeight = (fragCoord.x*2.f + (1.f-fragCoord.y)) / 3.f;
    bgWeight *= bgWeight;
    vec4 bgColor = g_background_color_a * (1.f - bgWeight) + g_background_color_b * bgWeight;
    bgColor.rgb *= bgColor.a;
    return bgColor;
}

#endif // CSGV_UTILS_GLSL
