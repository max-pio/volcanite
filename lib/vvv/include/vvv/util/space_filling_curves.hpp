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
//
// This class uses Morton index computation code by Fabian Giesen. Original code can be found at
// https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/

#pragma once

#include <glm/glm.hpp>

namespace vvv {

namespace sfc {

/* I don't want to waste more time on thinking about how to make this usable in an overloading fashion and still having static methods.
 * i.e. be able to replace one object from "CartesianCurve" to "MortonCurve" and use a different curve in a program.
 * static can't be virtual. Maybe create static objects here, for example from structs which inherit from an abstract parent blueprint?
 *
 * All Classes implement methods to convert positions in a brick to. */

class Cartesian {
  public:
    static size_t p2i(glm::uvec3 p, glm::uvec3 brick_size) { return static_cast<size_t>(p.x) + static_cast<size_t>(p.y) * brick_size.x + static_cast<size_t>(p.z) * brick_size.x * brick_size.y; }
    static glm::uvec3 i2p(size_t i, glm::uvec3 brick_size) { return {i % brick_size.x, (i / brick_size.x) % brick_size.y, (i / brick_size.x / brick_size.y) % brick_size.z}; }
};

// TODO: add asserts for position / index limits that can be indexed using 2D / 3D Morton codes in 32 or 64 bit

class Morton2D {
  public:
    static uint32_t p2i(glm::uvec2 p) { return (Part1By1(p.y) << 1) + Part1By1(p.x); }

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

class Morton3D {
  public:
    // the 32 bit variants can work with up to 10 bits per positional component
    static uint32_t p2i(glm::uvec3 p) {
        assert(glm::all(glm::lessThan(p, glm::uvec3(1024u))) && "32 Bit Morton code processing only works for dimensions up to (1023, 1023, 1023) (10 bit per component)");
        return (Part1By2(p.z) << 2) + (Part1By2(p.y) << 1) + Part1By2(p.x);
    }
    static glm::uvec3 i2p(uint32_t i) {
        assert(i < 1073741823 && "32 Bit Morton code processing only works for dimensions up to (1023, 1023, 1023) (10 bit per component)");
        return glm::uvec3(Compact1By2(i >> 0), Compact1By2(i >> 1), Compact1By2(i >> 2));
    }

    // the 64 bit variants can work with up to 20 bits per positional component
    static uint64_t p2i_64(glm::uvec3 p) {
        assert(glm::all(glm::lessThan(p, glm::uvec3(2097152u))) && "64 Bit Morton code processing only works for dimensions up to (2097151, 2097151, 2097151) (21 bit per component)");
        return (Part1By2_64(p.z) << 2) + (Part1By2_64(p.y) << 1) + Part1By2_64(p.x);
    }
    static glm::uvec3 i2p_64(uint64_t i) {
        assert(i < 9223372036854775807ul && "64 Bit Morton code processing only works for dimensions up to (2097151, 2097151, 2097151) (21 bit per component)");
        return glm::uvec3(Compact1By2_64(i >> 0), Compact1By2_64(i >> 1), Compact1By2_64(i >> 2));
    }

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

    // "Insert" two 0 bits after each of the 20 low bits of x
    static uint64_t Part1By2_64(uint64_t x) {
        x &= 0x1fffff;                         // take 20 bit pairs
        x = (x | x << 32) & 0x1f00000000ffff;  // 16 bits
        x = (x | x << 16) & 0x1f0000ff0000ff;  // 8 bits
        x = (x | x << 8) & 0x100f00f00f00f00f; // ...
        x = (x | x << 4) & 0x10c30c30c30c30c3;
        x = (x | x << 2) & 0x1249249249249249;
        return x;
    }

    // Inverse of Part1By1 - "delete" all odd-indexed bits
    static uint64_t Compact1By2_64(uint64_t x) {
        x &= 0x1249249249249249;
        x = (x ^ (x >> 2)) & 0x10c30c30c30c30c3;
        x = (x ^ (x >> 4)) & 0x100f00f00f00f00f;
        x = (x ^ (x >> 8)) & 0x1f0000ff0000ff;
        x = (x ^ (x >> 16)) & 0x1f00000000ffff;
        x = (x ^ (x >> 32)) & 0x1fffff;
        return x;
    }
};

} // namespace sfc

} // namespace vvv
