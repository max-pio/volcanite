#pragma once

#include <glm/glm.hpp>

namespace vvv {

namespace sfc {

/**
 * I don't want to waste more time on thinking about how to make this usable in an overloading fashion and still having static methods.
 * i.e. be able to replace one object from "CartesianCurve" to "MortonCurve" and use a different curve in a program.
 * static can't be virtual. Maybe create static objects here, for example from structs which inherit from an abstract parent blueprint?
 *
 * All Classes implement methods to convert positions in a brick to
 */

class Cartesian {
public:
    static size_t p2i(glm::uvec3 p, glm::uvec3 brick_size) { return static_cast<size_t>(p.x) + static_cast<size_t>(p.y) * brick_size.x + static_cast<size_t>(p.z) * brick_size.x * brick_size.y; }
    static glm::uvec3 i2p(size_t i, glm::uvec3 brick_size) { return {i % brick_size.x, (i / brick_size.x) % brick_size.y, (i / brick_size.x / brick_size.y) % brick_size.z}; }
};

/**
 * https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
 */
class Morton2D {
public:
    static uint32_t p2i(glm::uvec3 p) { return (Part1By1(p.y) << 1) + Part1By1(p.x); }

    static glm::uvec2 i2p(uint32_t i) { return glm::uvec2(Compact1By1(i >> 0), Compact1By1(i >> 1)); }

private:
    // "Insert" a 0 bit after each of the 16 low bits of x
    static uint32_t Part1By1(uint32_t x) {
        x &= 0x0000ffff;                 // x = ---- ---- ---- ---- fedc ba98 7654 3210
        x = (x ^ (x << 8)) & 0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
        x = (x ^ (x << 4)) & 0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
        x = (x ^ (x << 2)) & 0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
        x = (x ^ (x << 1)) & 0x55555555; // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
        return x;
    }

    // Inverse of Part1By1 - "delete" all odd-indexed bits
    static uint32_t Compact1By1(uint32_t x) {
        x &= 0x55555555;                 // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
        x = (x ^ (x >> 1)) & 0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
        x = (x ^ (x >> 2)) & 0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
        x = (x ^ (x >> 4)) & 0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
        x = (x ^ (x >> 8)) & 0x0000ffff; // x = ---- ---- ---- ---- fedc ba98 7654 3210
        return x;
    }
};

/**
 * https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
 */
class Morton3D {
public:
    static uint32_t p2i(glm::uvec3 p) { return (Part1By2(p.z) << 2) + (Part1By2(p.y) << 1) + Part1By2(p.x); }
    static glm::uvec3 i2p(uint32_t i) { return glm::uvec3(Compact1By2(i >> 0), Compact1By2(i >> 1), Compact1By2(i >> 2)); }

private:
    // "Insert" two 0 bits after each of the 10 low bits of x
    static uint32_t Part1By2(uint32_t x) {
        x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
        x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x ^ (x << 8)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x ^ (x << 4)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x ^ (x << 2)) & 0x09249249;  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        return x;
    }

    // Inverse of Part1By1 - "delete" all odd-indexed bits
    static uint32_t Compact1By2(uint32_t x) {
        x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        x = (x ^ (x >> 2)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x ^ (x >> 4)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x ^ (x >> 8)) & 0xff0000ff;  // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
        return x;
    }
};

} // namespace sfc

} // namespace vvv
