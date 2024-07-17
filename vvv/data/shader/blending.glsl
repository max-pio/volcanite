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

/// Back to front blending, also called the 'over operator'. [Porter & Duff 1984]
vec4 blend_back_to_front_premultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb * (1.0-new_sample.w) + new_sample.rgb,
    accumulator.a * (1.0-new_sample.w) + new_sample.a
    );
}

/// Back to front blending, also called the 'over operator' for postmultiplied colors.
/// Synonyms for postmultiplied: straight, non-associated color, ...
vec4 blend_back_to_front_postmultiplied(vec4 accumulator, vec4 new_sample) {
    return vec4(
    accumulator.rgb * (1.0-new_sample.w) + new_sample.rgb * new_sample.a,
    accumulator.a * (1.0-new_sample.w) + new_sample.a
    );
}

#endif // BLENDING_H