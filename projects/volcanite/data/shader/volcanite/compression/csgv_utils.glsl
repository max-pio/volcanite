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

bool isLabelVisible(uint label) {
    return label != g_empty_label && label >= g_label_minmax.x && label <= g_label_minmax.y;
}

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

#endif // CSGV_UTILS_GLSL
