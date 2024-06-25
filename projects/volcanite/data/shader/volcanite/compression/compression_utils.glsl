#ifndef COMPRESSION_UTILS_GLSL
#define COMPRESSION_UTILS_GLSL



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

#endif // COMPRESSION_UTILS_GLSL
