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

#define WRITE_DEPTH(X, D) imageStore(outDepth, X, D * 8.f).r
#define READ_DEPTH(X) imageLoad(outDepth, X).r / 8.f

// Indexing and vector utils -------------------------------------------------------------------------------------------
uint brick_to_1D(uvec3 brick_idx, uvec3 brick_dim) {
    return brick_idx.x + brick_dim.x * (brick_idx.y + brick_dim.y * brick_idx.z);
}

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

vec4 get_color(uint label) {
    int m = getMaterial(label);
#ifndef NDEBUG
    assert(!any(isnan(vec4(g_materials[m].discrIntervalMin,  g_materials[m].discrIntervalMax, g_materials[m].tfIntervalMin, g_materials[m].tfIntervalMax))), "NaN in shader attribute limits");
#endif
    return vec4(texture(s_transferFunctions[m], map(getAttribute(label, g_materials[m].tfAttributeStart), g_materials[m].tfIntervalMin, g_materials[m].tfIntervalMax, 0.f, 1.f)).rgb, 1.f);
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
