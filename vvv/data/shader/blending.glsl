#ifndef BLENDING_H
#define BLENDING_H

vec4 blend_front_to_back_postmultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb + new_sample.rgb * (1.0-accumulator.w) * new_sample.a,
    accumulator.a + (1.0-accumulator.w) * new_sample.a
    );
}

vec4 blend_front_to_back_premultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb + new_sample.rgb * (1.0-accumulator.w),
    accumulator.a + (1.0-accumulator.w) * new_sample.a
    );
}

/**
 * Back to front blending, also called the 'over operator'. [Porter & Duff 1984]
 */
vec4 blend_back_to_front_premultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb * (1.0-new_sample.w) + new_sample.rgb,
    accumulator.a * (1.0-new_sample.w) + new_sample.a
    );
}

/**
 * Back to front blending, also called the 'over operator' for postmultiplied colors.
 *
 * Synonyms for postmultiplied: straight, non-associated color, ...
 */
vec4 blend_back_to_front_postmultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb * (1.0-new_sample.w) + new_sample.rgb * new_sample.a,
    accumulator.a * (1.0-new_sample.w) + new_sample.a
    );
}

#endif /* BLENDING_H */