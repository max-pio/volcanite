#ifndef VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
#define VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL

// For better readability in csgv_renderer.comp, most of the debug visualizations and functionality is encapsualted in
// functions in this header. As these functions use other definitions from csgv_renderer.comp it should be included
// right above its main() function.

#define ENALBE_CSGV_DEBUGGING

#include "csgv_materials.glsl"
#include "debug_colormaps.glsl"

// blend a cache visualization over the pixels color
void DEBUG_img_cache(ivec2 pixel, inout vec4 color, bool enabled) {
#ifdef ENALBE_CSGV_DEBUGGING
    if (!enabled)
        return;
#endif
}

#endif // VOLCANITE_CSGV_DEBUG_RESOLVE_GLSL
