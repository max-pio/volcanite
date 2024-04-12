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

#ifndef PI
    #define PI 3.14159265359f
#endif
#ifndef INV_PI
    #define INV_PI 0.3183098861837907f
#endif
#ifndef TWO_PI
    #define TWO_PI 6.28318530718f
#endif
#ifndef INV_TWO_PI
    #define INV_TWO_PI 0.15915494309
#endif


// Indexing and vector utils -------------------------------------------------------------------------------------------
uint brick_to_1D(uvec3 brick_idx, uvec3 brick_dim) {
    return brick_idx.x + brick_dim.x * (brick_idx.y + brick_dim.y * brick_idx.z);
}

// adds the element offset (one unit = 4 byte) to the 64 bit address represented in an uvec2
uvec2 bufferAddressAdd(uvec2 address, uint uint_elem_offset) {
    uint carry;
    // the offset is measured in uints but we have to add 4 byte per uint. To prevent uint overflow, we repeat the op:
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    return address;
}

// substracts the element offset (one unit = 4 byte) from the 64 bit address represented in an uvec2
uvec2 bufferAddressSub(uvec2 address, uint uint_elem_offset) {
    uint borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    return address;
}

EncodingRef getBrickEncodingRef(uint brick_encoding_start) {
    return EncodingRef(bufferAddressAdd(g_encoding_buffer_address, brick_encoding_start));
}

#ifdef SEPARATE_DETAIL
EncodingRef getBrickDetailEncodingRef(uint brick_detail_start) {
    return EncodingRef(bufferAddressAdd(g_detail_buffer_address, brick_detail_start));
}
#endif


int maxComponent(vec3 v) {
    if (v.x < v.y) {
        if (v.x < v.z) {
            return 0;
        } else {
            return 2;
        }
    } else {
        if (v.y < v.z) {
            return 1;
        } else {
            return 2;
        }
    }
}

float map(float v, float v_min, float v_max, float new_min, float new_max) {
    return (v - v_min) / (v_max - v_min) * (new_max - new_min) + new_min;
}


// Materials -----------------------------------------------------------------------------------------------------------
float getAttribute(uint label, uint attributeStart) {
    // attributeStart > valid buffer size means that we use the voxel label direclty (csgv_id)
    if(attributeStart == LABEL_AS_ATTRIBUTE)
        return float(label);
    else
        return g_attributes[attributeStart + label];
}

/// Returns the first material where the discriminator attribute falls into the discriminator interval
int getMaterial(uint label) {
    for(int m = 0; m <= g_max_active_material; m++) {
        if(getAttribute(label, g_materials[m].discrAttributeStart) >= g_materials[m].discrIntervalMin
        && getAttribute(label, g_materials[m].discrAttributeStart) <= g_materials[m].discrIntervalMax)
        return m;
    }
    return -1;
}

bool isLabelVisible(uint label) {
    return getMaterial(label) >= 0;
    // old: return label != g_empty_label && label >= g_label_minmax.x && label <= g_label_minmax.y;
}

vec4 getColor(uint label, int material) {
    // strange bug (occurs on my old AMD RX480 card):
    // if the texture(..) call accesses a sampler from the array based on the variable m, the same sampler is selected
    // for all threads in a warp. This may be a problem of (non)-uniform control flow:
    // see https://www.khronos.org/opengl/wiki/Sampler_(GLSL)#Non-uniform_flow_control
    // or  https://stackoverflow.com/questions/53734640/will-any-of-the-following-texture-lookups-cause-undefined-behavior-non-uniform
    //
    // This should not be an issue with GLSL version >= 4!
    // Anyways, here's a strange fix by "forcing" non-uniform control flow:
    if(material == 0)
        return vec4(textureLod(s_transferFunctions[0], map(getAttribute(label, g_materials[0].tfAttributeStart), g_materials[0].tfIntervalMin, g_materials[0].tfIntervalMax, 0.f, 1.f), 0.f).rgb, g_materials[0].opacity);

#ifndef NDEBUG
    assertf(material >= 0 && material <= g_max_active_material, "material %i assigned to label is invalid", material);
    assert(!any(isnan(vec4(g_materials[material].discrIntervalMin,  g_materials[material].discrIntervalMax, g_materials[material].tfIntervalMin, g_materials[material].tfIntervalMax))), "NaN in shader attribute limits");
#endif
    return vec4(textureLod(s_transferFunctions[material], map(getAttribute(label, g_materials[material].tfAttributeStart), g_materials[material].tfIntervalMin, g_materials[material].tfIntervalMax, 0.f, 1.f), 0.f).rgb, g_materials[material].opacity);
}

// Background color ----------------------------------------------------------------------------------------------------

/// The background is a tilted interpolation between two colors g_background_color_a and g_background_color_b
vec4 get_background_color(vec2 fragCoord) {
    float bgWeight = (fragCoord.x*2.f + (1.f-fragCoord.y)) / 3.f;
    bgWeight *= bgWeight;
    vec4 bgColor = g_background_color_a * (1.f - bgWeight) + g_background_color_b * bgWeight;
    bgColor.rgb *= bgColor.a;
    return bgColor;
}

#endif // CSGV_UTILS_GLSL
