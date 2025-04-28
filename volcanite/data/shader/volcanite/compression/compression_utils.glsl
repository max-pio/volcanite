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

#ifndef VOLCANITE_COMPRESSION_UTILS_GLSL
#define VOLCANITE_COMPRESSION_UTILS_GLSL

// map brick + frame combination to 1D index, for example to access the index_buffer
int brick_pos2idx(ivec3 brick_idx, ivec3 brick_count, int frame) {
    return brick_idx.x + brick_count.x * (brick_idx.y + brick_count.y * (brick_idx.z + brick_count.z * frame));
}
int brick_pos2idx(ivec3 brick_idx, ivec3 brick_count) {
    return brick_pos2idx(brick_idx, brick_count, 0);
}

uint brick_pos2idx(uvec3 brick_idx, uvec3 brick_count, uint frame) {
    return brick_idx.x + brick_count.x * (brick_idx.y + brick_count.y * (brick_idx.z + brick_count.z * frame));
}
uint brick_pos2idx(uvec3 brick_idx, uvec3 brick_count) {
    return brick_pos2idx(brick_idx, brick_count, 0u);
}

#endif // VOLCANITE_COMPRESSION_UTILS_GLSL
