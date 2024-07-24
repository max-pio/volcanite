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

// GBuffer ----------------------------------------------------------------------------------------


/// return a value that can be stored in an RG8 format to indicate an invalid G-Buffer entry
uvec2 invalidGBufferRG8() {
    // first two bits 11 is invalid. set everything to 1.
    // we could use other bits to store if this is a background / no hit sample
    return uvec2(0xFF, 0xFF);
}

/// pack the given attributes in a value that can be stored in an RG8 format
uvec2 packGBufferRG8(uint label, vec3 normal, float normalized_depth) {
    uvec2 packed = uvec2(0u);

    // 3 bits for -/+ {(1,0,0) | (0,1,0) | (0,0,1)} axis-aligned normal
    // first two bits denote the axis: 00=x 01=y 10=z or an invalid G-Buffer sample with 11
    packed.x |= clamp(uint(dot(abs(normal), vec3(0.f, 1.f, 2.f))), 0u, 2u);
    // third bit is the sign: 0 positive, 1 negative
    packed.x |= dot(normal, vec3(1.f, 1.f, 1.f)) < 0.f ? 4u : 0u;

    // 5 bits for label hash
    packed.x |= ((label ^ (label >> 16)) % 32) << 3;

    // 8 bits for depth
    packed.y = uint(clamp(normalized_depth, 0.f, 1.f) * 255.f);

    return packed;
}

/// unpack the given RG8 G-Buffer value into attributes. Returns true if the G-Buffer sample was valid.
bool unpackGBufferRG8(uvec2 g_buffer_packed, out uint label, out vec3 normal, out float normalized_depth) {
    // first two bits 11 is an invalid buffer entry
    if ((g_buffer_packed.x & 0x3u) == 3u)
        return false;

    // 3 bits for normal
    normal = vec3(0.f);
    normal[(g_buffer_packed.x & 0x3u)] = 1.f - 2.f * float((g_buffer_packed.x >> 2) & 0x1u);

    // 5 bits for label hash. This is not bijective, so we just return the hash
    label = (g_buffer_packed.x >> 3u) & 0x1Fu;

    // 8 bits for normalized depth
    normalized_depth = float(g_buffer_packed.y) / 255.f;

    return true;
}

#endif // CSGV_UTILS_GLSL
