#ifndef MORTON_GLSL
#define MORTON_GLSL

// 2D ------------------------------------------------------------------------------------------------------------------

// "Insert" a 0 bit after each of the 16 low bits of x
uint _morton_Part1By1(uint x) {
    x &= 0x0000ffffu;                 // x = ---- ---- ---- ---- fedc ba98 7654 3210
    x = (x ^ (x << 8u)) & 0x00ff00ffu; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
    x = (x ^ (x << 4u)) & 0x0f0f0f0fu; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
    x = (x ^ (x << 2u)) & 0x33333333u; // x = --fe --dc --ba --98 --76 --54 --32 --10
    x = (x ^ (x << 1u)) & 0x55555555u; // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
    return x;
}

// Inverse of Part1By1 - "delete" all odd-indexed bits
uint _morton_Compact1By1(uint x) {
    x &= 0x55555555u;                 // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
    x = (x ^ (x >> 1u)) & 0x33333333u; // x = --fe --dc --ba --98 --76 --54 --32 --10
    x = (x ^ (x >> 2u)) & 0x0f0f0f0fu; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
    x = (x ^ (x >> 4u)) & 0x00ff00ffu; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
    x = (x ^ (x >> 8u)) & 0x0000ffffu; // x = ---- ---- ---- ---- fedc ba98 7654 3210
    return x;
}

uint morton2Dp2i(uvec3 p) { return (_morton_Part1By1(p.y) << 1) + _morton_Part1By1(p.x); }
uvec2 morton2Di2p(uint i) { return uvec2(_morton_Compact1By1(i >> 0), _morton_Compact1By1(i >> 1)); }

// 3D ------------------------------------------------------------------------------------------------------------------

// "Insert" two 0 bits after each of the 10 low bits of x
uint _morton_Part1By2(uint x) {
    x &= 0x000003ffu;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
    x = (x ^ (x << 16u)) & 0xff0000ffu; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x << 8u)) & 0x0300f00fu;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x << 4u)) & 0x030c30c3u;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x << 2u)) & 0x09249249u;  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    return x;
}

// Inverse of Part1By1 - "delete" all odd-indexed bits
uint _morton_Compact1By2(uint x) {
    x &= 0x09249249u;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x >> 2u)) & 0x030c30c3u;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x >> 4u)) & 0x0300f00fu;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x >> 8u)) & 0xff0000ffu;  // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x >> 16u)) & 0x000003ffu; // x = ---- ---- ---- ---- ---- --98 7654 3210
    return x;
}

uint morton3Dp2i(uvec3 p) { return (_morton_Part1By2(p.z) << 2) + (_morton_Part1By2(p.y) << 1) + _morton_Part1By2(p.x); }
uvec3 morton3Di2p(uint i) { return uvec3(_morton_Compact1By2(i >> 0), _morton_Compact1By2(i >> 1), _morton_Compact1By2(i >> 2)); }




#endif // MORTON_GLSL