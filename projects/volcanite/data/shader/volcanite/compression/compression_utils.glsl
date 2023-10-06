#ifndef COMPRESSION_UTILS_GLSL
#define COMPRESSION_UTILS_GLSL



// map brick + frame combination to 1D index, for example to access the index_buffer
int brick_to_1D(ivec3 brick_idx, ivec3 brick_dim, int frame) {
    return brick_idx.x + brick_dim.x * (brick_idx.y + brick_dim.y * (brick_idx.z + brick_dim.z * frame));
}
int brick_to_1D(ivec3 brick_idx, ivec3 brick_dim) {
    return brick_to_1D(brick_idx, brick_dim, 0);
}

uint brick_to_1D(uvec3 brick_idx, uvec3 brick_dim, uint frame) {
    return brick_idx.x + brick_dim.x * (brick_idx.y + brick_dim.y * (brick_idx.z + brick_dim.z * frame));
}
uint brick_to_1D(uvec3 brick_idx, uvec3 brick_dim) {
    return brick_to_1D(brick_idx, brick_dim, 0u);
}

#endif // COMPRESSION_UTILS_GLSL
