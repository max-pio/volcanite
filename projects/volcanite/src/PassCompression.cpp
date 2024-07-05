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

#include "volcanite/compression/PassCompression.hpp"

#include "vvv/util/util.hpp"
#include "vvv/volren/Volume.hpp"


std::string frame_path(const std::string& directory_path, int frame) {
    // TODO fix the 00
    return directory_path + (directory_path.back() != '/' && directory_path.back() != '\\' ? "/" : "") + "cells_frame" + vvv::leading_zeros_string(frame, 3) + ".raw";
}

int frames_of_dataset(const std::string& directory_path) {
    return 1;
}

// TODO CLEAN THIS MESS UP! MOVE TO ANOTHER HEADER. AND: ADD THE SPIRAL CURVE AND DIAGONAL CURVE

const glm::ivec3 hilbert8x8x8[] = {glm::ivec3(0,0,0), glm::ivec3(1,0,0), glm::ivec3(1,0,1), glm::ivec3(0,0,1), glm::ivec3(0,1,1), glm::ivec3(1,1,1), glm::ivec3(1,1,0),
                              glm::ivec3(0,1,0), glm::ivec3(0,2,0), glm::ivec3(0,3,0), glm::ivec3(0,3,1), glm::ivec3(0,2,1), glm::ivec3(1,2,1), glm::ivec3(1,3,1), glm::ivec3(1,3,0), glm::ivec3(1,2,0),
                              glm::ivec3(2,2,0), glm::ivec3(2,3,0), glm::ivec3(3,3,0), glm::ivec3(3,2,0), glm::ivec3(3,2,1), glm::ivec3(3,3,1), glm::ivec3(2,3,1), glm::ivec3(2,2,1), glm::ivec3(2,1,1),
                              glm::ivec3(2,1,0), glm::ivec3(3,1,0), glm::ivec3(3,1,1), glm::ivec3(3,0,1), glm::ivec3(3,0,0), glm::ivec3(2,0,0), glm::ivec3(2,0,1), glm::ivec3(2,0,2), glm::ivec3(2,0,3),
                              glm::ivec3(3,0,3), glm::ivec3(3,0,2), glm::ivec3(3,1,2), glm::ivec3(3,1,3), glm::ivec3(2,1,3), glm::ivec3(2,1,2), glm::ivec3(2,2,2), glm::ivec3(2,3,2), glm::ivec3(3,3,2),
                              glm::ivec3(3,2,2), glm::ivec3(3,2,3), glm::ivec3(3,3,3), glm::ivec3(2,3,3), glm::ivec3(2,2,3), glm::ivec3(1,2,3), glm::ivec3(1,3,3), glm::ivec3(1,3,2), glm::ivec3(1,2,2), 
                              glm::ivec3(0,2,2), glm::ivec3(0,3,2), glm::ivec3(0,3,3), glm::ivec3(0,2,3), glm::ivec3(0,1,3), glm::ivec3(1,1,3), glm::ivec3(1,1,2), glm::ivec3(0,1,2), glm::ivec3(0,0,2),
                              glm::ivec3(1,0,2), glm::ivec3(1,0,3), glm::ivec3(0,0,3), glm::ivec3(0,0,4), glm::ivec3(1,0,4), glm::ivec3(1,1,4), glm::ivec3(0,1,4), glm::ivec3(0,1,5), glm::ivec3(1,1,5),
                              glm::ivec3(1,0,5), glm::ivec3(0,0,5), glm::ivec3(0,0,6), glm::ivec3(0,0,7), glm::ivec3(0,1,7), glm::ivec3(0,1,6), glm::ivec3(1,1,6), glm::ivec3(1,1,7), glm::ivec3(1,0,7),
                              glm::ivec3(1,0,6), glm::ivec3(2,0,6), glm::ivec3(2,0,7), glm::ivec3(3,0,7), glm::ivec3(3,0,6), glm::ivec3(3,1,6), glm::ivec3(3,1,7), glm::ivec3(2,1,7), glm::ivec3(2,1,6),
                              glm::ivec3(2,1,5), glm::ivec3(2,0,5), glm::ivec3(3,0,5), glm::ivec3(3,1,5), glm::ivec3(3,1,4), glm::ivec3(3,0,4), glm::ivec3(2,0,4), glm::ivec3(2,1,4), glm::ivec3(2,2,4),
                              glm::ivec3(2,3,4), glm::ivec3(3,3,4), glm::ivec3(3,2,4), glm::ivec3(3,2,5), glm::ivec3(3,3,5), glm::ivec3(2,3,5), glm::ivec3(2,2,5), glm::ivec3(2,2,6), glm::ivec3(2,2,7),
                              glm::ivec3(3,2,7), glm::ivec3(3,2,6), glm::ivec3(3,3,6), glm::ivec3(3,3,7), glm::ivec3(2,3,7), glm::ivec3(2,3,6), glm::ivec3(1,3,6), glm::ivec3(1,3,7), glm::ivec3(1,2,7), 
                              glm::ivec3(1,2,6), glm::ivec3(0,2,6), glm::ivec3(0,2,7), glm::ivec3(0,3,7), glm::ivec3(0,3,6), glm::ivec3(0,3,5), glm::ivec3(1,3,5), glm::ivec3(1,2,5), glm::ivec3(0,2,5), 
                              glm::ivec3(0,2,4), glm::ivec3(1,2,4), glm::ivec3(1,3,4), glm::ivec3(0,3,4), glm::ivec3(0,4,4), glm::ivec3(0,5,4), glm::ivec3(1,5,4), glm::ivec3(1,4,4), glm::ivec3(1,4,5), 
                              glm::ivec3(1,5,5), glm::ivec3(0,5,5), glm::ivec3(0,4,5), glm::ivec3(0,4,6), glm::ivec3(0,4,7), glm::ivec3(1,4,7), glm::ivec3(1,4,6), glm::ivec3(1,5,6), glm::ivec3(1,5,7), 
                              glm::ivec3(0,5,7), glm::ivec3(0,5,6), glm::ivec3(0,6,6), glm::ivec3(0,6,7), glm::ivec3(0,7,7), glm::ivec3(0,7,6), glm::ivec3(1,7,6), glm::ivec3(1,7,7), glm::ivec3(1,6,7), 
                              glm::ivec3(1,6,6), glm::ivec3(1,6,5), glm::ivec3(0,6,5), glm::ivec3(0,7,5), glm::ivec3(1,7,5), glm::ivec3(1,7,4), glm::ivec3(0,7,4), glm::ivec3(0,6,4), glm::ivec3(1,6,4), 
                              glm::ivec3(2,6,4), glm::ivec3(3,6,4), glm::ivec3(3,7,4), glm::ivec3(2,7,4), glm::ivec3(2,7,5), glm::ivec3(3,7,5), glm::ivec3(3,6,5), glm::ivec3(2,6,5), glm::ivec3(2,6,6), 
                              glm::ivec3(2,6,7), glm::ivec3(2,7,7), glm::ivec3(2,7,6), glm::ivec3(3,7,6), glm::ivec3(3,7,7), glm::ivec3(3,6,7), glm::ivec3(3,6,6), glm::ivec3(3,5,6), glm::ivec3(3,5,7), 
                              glm::ivec3(2,5,7), glm::ivec3(2,5,6), glm::ivec3(2,4,6), glm::ivec3(2,4,7), glm::ivec3(3,4,7), glm::ivec3(3,4,6), glm::ivec3(3,4,5), glm::ivec3(3,5,5), glm::ivec3(2,5,5), 
                              glm::ivec3(2,4,5), glm::ivec3(2,4,4), glm::ivec3(2,5,4), glm::ivec3(3,5,4), glm::ivec3(3,4,4), glm::ivec3(3,4,3), glm::ivec3(3,5,3), glm::ivec3(3,5,2), glm::ivec3(3,4,2), 
                              glm::ivec3(2,4,2), glm::ivec3(2,5,2), glm::ivec3(2,5,3), glm::ivec3(2,4,3), glm::ivec3(1,4,3), glm::ivec3(0,4,3), glm::ivec3(0,4,2), glm::ivec3(1,4,2), glm::ivec3(1,5,2), 
                              glm::ivec3(0,5,2), glm::ivec3(0,5,3), glm::ivec3(1,5,3), glm::ivec3(1,6,3), glm::ivec3(0,6,3), glm::ivec3(0,7,3), glm::ivec3(1,7,3), glm::ivec3(1,7,2), glm::ivec3(0,7,2), 
                              glm::ivec3(0,6,2), glm::ivec3(1,6,2), glm::ivec3(2,6,2), glm::ivec3(2,6,3), glm::ivec3(2,7,3), glm::ivec3(2,7,2), glm::ivec3(3,7,2), glm::ivec3(3,7,3), glm::ivec3(3,6,3), 
                              glm::ivec3(3,6,2), glm::ivec3(3,6,1), glm::ivec3(3,6,0), glm::ivec3(3,7,0), glm::ivec3(3,7,1), glm::ivec3(2,7,1), glm::ivec3(2,7,0), glm::ivec3(2,6,0), glm::ivec3(2,6,1), 
                              glm::ivec3(1,6,1), glm::ivec3(0,6,1), glm::ivec3(0,7,1), glm::ivec3(1,7,1), glm::ivec3(1,7,0), glm::ivec3(0,7,0), glm::ivec3(0,6,0), glm::ivec3(1,6,0), glm::ivec3(1,5,0), 
                              glm::ivec3(0,5,0), glm::ivec3(0,5,1), glm::ivec3(1,5,1), glm::ivec3(1,4,1), glm::ivec3(0,4,1), glm::ivec3(0,4,0), glm::ivec3(1,4,0), glm::ivec3(2,4,0), glm::ivec3(2,5,0), 
                              glm::ivec3(2,5,1), glm::ivec3(2,4,1), glm::ivec3(3,4,1), glm::ivec3(3,5,1), glm::ivec3(3,5,0), glm::ivec3(3,4,0), glm::ivec3(4,4,0), glm::ivec3(4,5,0), glm::ivec3(4,5,1), 
                              glm::ivec3(4,4,1), glm::ivec3(5,4,1), glm::ivec3(5,5,1), glm::ivec3(5,5,0), glm::ivec3(5,4,0), glm::ivec3(6,4,0), glm::ivec3(7,4,0), glm::ivec3(7,4,1), glm::ivec3(6,4,1), 
                              glm::ivec3(6,5,1), glm::ivec3(7,5,1), glm::ivec3(7,5,0), glm::ivec3(6,5,0), glm::ivec3(6,6,0), glm::ivec3(7,6,0), glm::ivec3(7,7,0), glm::ivec3(6,7,0), glm::ivec3(6,7,1), 
                              glm::ivec3(7,7,1), glm::ivec3(7,6,1), glm::ivec3(6,6,1), glm::ivec3(5,6,1), glm::ivec3(5,6,0), glm::ivec3(5,7,0), glm::ivec3(5,7,1), glm::ivec3(4,7,1), glm::ivec3(4,7,0), 
                              glm::ivec3(4,6,0), glm::ivec3(4,6,1), glm::ivec3(4,6,2), glm::ivec3(4,6,3), glm::ivec3(4,7,3), glm::ivec3(4,7,2), glm::ivec3(5,7,2), glm::ivec3(5,7,3), glm::ivec3(5,6,3),
                              glm::ivec3(5,6,2), glm::ivec3(6,6,2), glm::ivec3(7,6,2), glm::ivec3(7,7,2), glm::ivec3(6,7,2), glm::ivec3(6,7,3), glm::ivec3(7,7,3), glm::ivec3(7,6,3), glm::ivec3(6,6,3),
                              glm::ivec3(6,5,3), glm::ivec3(7,5,3), glm::ivec3(7,5,2), glm::ivec3(6,5,2), glm::ivec3(6,4,2), glm::ivec3(7,4,2), glm::ivec3(7,4,3), glm::ivec3(6,4,3), glm::ivec3(5,4,3),
                              glm::ivec3(5,5,3), glm::ivec3(5,5,2), glm::ivec3(5,4,2), glm::ivec3(4,4,2), glm::ivec3(4,5,2), glm::ivec3(4,5,3), glm::ivec3(4,4,3), glm::ivec3(4,4,4), glm::ivec3(4,5,4), 
                              glm::ivec3(5,5,4), glm::ivec3(5,4,4), glm::ivec3(5,4,5), glm::ivec3(5,5,5), glm::ivec3(4,5,5), glm::ivec3(4,4,5), glm::ivec3(4,4,6), glm::ivec3(4,4,7), glm::ivec3(5,4,7), 
                              glm::ivec3(5,4,6), glm::ivec3(5,5,6), glm::ivec3(5,5,7), glm::ivec3(4,5,7), glm::ivec3(4,5,6), glm::ivec3(4,6,6), glm::ivec3(4,6,7), glm::ivec3(4,7,7), glm::ivec3(4,7,6), 
                              glm::ivec3(5,7,6), glm::ivec3(5,7,7), glm::ivec3(5,6,7), glm::ivec3(5,6,6), glm::ivec3(5,6,5), glm::ivec3(4,6,5), glm::ivec3(4,7,5), glm::ivec3(5,7,5), glm::ivec3(5,7,4), 
                              glm::ivec3(4,7,4), glm::ivec3(4,6,4), glm::ivec3(5,6,4), glm::ivec3(6,6,4), glm::ivec3(7,6,4), glm::ivec3(7,7,4), glm::ivec3(6,7,4), glm::ivec3(6,7,5), glm::ivec3(7,7,5), 
                              glm::ivec3(7,6,5), glm::ivec3(6,6,5), glm::ivec3(6,6,6), glm::ivec3(6,6,7), glm::ivec3(6,7,7), glm::ivec3(6,7,6), glm::ivec3(7,7,6), glm::ivec3(7,7,7), glm::ivec3(7,6,7),
                              glm::ivec3(7,6,6), glm::ivec3(7,5,6), glm::ivec3(7,5,7), glm::ivec3(6,5,7), glm::ivec3(6,5,6), glm::ivec3(6,4,6), glm::ivec3(6,4,7), glm::ivec3(7,4,7), glm::ivec3(7,4,6), 
                              glm::ivec3(7,4,5), glm::ivec3(7,5,5), glm::ivec3(6,5,5), glm::ivec3(6,4,5), glm::ivec3(6,4,4), glm::ivec3(6,5,4), glm::ivec3(7,5,4), glm::ivec3(7,4,4), glm::ivec3(7,3,4), 
                              glm::ivec3(6,3,4), glm::ivec3(6,2,4), glm::ivec3(7,2,4), glm::ivec3(7,2,5), glm::ivec3(6,2,5), glm::ivec3(6,3,5), glm::ivec3(7,3,5), glm::ivec3(7,3,6), glm::ivec3(7,3,7), 
                              glm::ivec3(7,2,7), glm::ivec3(7,2,6), glm::ivec3(6,2,6), glm::ivec3(6,2,7), glm::ivec3(6,3,7), glm::ivec3(6,3,6), glm::ivec3(5,3,6), glm::ivec3(5,3,7), glm::ivec3(4,3,7), 
                              glm::ivec3(4,3,6), glm::ivec3(4,2,6), glm::ivec3(4,2,7), glm::ivec3(5,2,7), glm::ivec3(5,2,6), glm::ivec3(5,2,5), glm::ivec3(5,3,5), glm::ivec3(4,3,5), glm::ivec3(4,2,5), 
                              glm::ivec3(4,2,4), glm::ivec3(4,3,4), glm::ivec3(5,3,4), glm::ivec3(5,2,4), glm::ivec3(5,1,4), glm::ivec3(5,0,4), glm::ivec3(4,0,4), glm::ivec3(4,1,4), glm::ivec3(4,1,5), 
                              glm::ivec3(4,0,5), glm::ivec3(5,0,5), glm::ivec3(5,1,5), glm::ivec3(5,1,6), glm::ivec3(5,1,7), glm::ivec3(4,1,7), glm::ivec3(4,1,6), glm::ivec3(4,0,6), glm::ivec3(4,0,7), 
                              glm::ivec3(5,0,7), glm::ivec3(5,0,6), glm::ivec3(6,0,6), glm::ivec3(6,0,7), glm::ivec3(6,1,7), glm::ivec3(6,1,6), glm::ivec3(7,1,6), glm::ivec3(7,1,7), glm::ivec3(7,0,7), 
                              glm::ivec3(7,0,6), glm::ivec3(7,0,5), glm::ivec3(6,0,5), glm::ivec3(6,1,5), glm::ivec3(7,1,5), glm::ivec3(7,1,4), glm::ivec3(6,1,4), glm::ivec3(6,0,4), glm::ivec3(7,0,4), 
                              glm::ivec3(7,0,3), glm::ivec3(6,0,3), glm::ivec3(6,0,2), glm::ivec3(7,0,2), glm::ivec3(7,1,2), glm::ivec3(6,1,2), glm::ivec3(6,1,3), glm::ivec3(7,1,3), glm::ivec3(7,2,3), 
                              glm::ivec3(7,3,3), glm::ivec3(7,3,2), glm::ivec3(7,2,2), glm::ivec3(6,2,2), glm::ivec3(6,3,2), glm::ivec3(6,3,3), glm::ivec3(6,2,3), glm::ivec3(5,2,3), glm::ivec3(5,3,3), 
                              glm::ivec3(4,3,3), glm::ivec3(4,2,3), glm::ivec3(4,2,2), glm::ivec3(4,3,2), glm::ivec3(5,3,2), glm::ivec3(5,2,2), glm::ivec3(5,1,2), glm::ivec3(5,1,3), glm::ivec3(4,1,3), 
                              glm::ivec3(4,1,2), glm::ivec3(4,0,2), glm::ivec3(4,0,3), glm::ivec3(5,0,3), glm::ivec3(5,0,2), glm::ivec3(5,0,1), glm::ivec3(5,0,0), glm::ivec3(4,0,0), glm::ivec3(4,0,1), 
                              glm::ivec3(4,1,1), glm::ivec3(4,1,0), glm::ivec3(5,1,0), glm::ivec3(5,1,1), glm::ivec3(5,2,1), glm::ivec3(5,3,1), glm::ivec3(4,3,1), glm::ivec3(4,2,1), glm::ivec3(4,2,0), 
                              glm::ivec3(4,3,0), glm::ivec3(5,3,0), glm::ivec3(5,2,0), glm::ivec3(6,2,0), glm::ivec3(6,3,0), glm::ivec3(6,3,1), glm::ivec3(6,2,1), glm::ivec3(7,2,1), glm::ivec3(7,3,1), 
                              glm::ivec3(7,3,0), glm::ivec3(7,2,0), glm::ivec3(7,1,0), glm::ivec3(6,1,0), glm::ivec3(6,1,1), glm::ivec3(7,1,1), glm::ivec3(7,0,1), glm::ivec3(6,0,1), glm::ivec3(6,0,0), glm::ivec3(7,0,0)};



// Inverse of part_by_two - "delete" all bits not at positions divisible by 3
glm::uint32 morton_encode_by_two(glm::uint32 x)
{
    x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x >>  2)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x >>  4)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x >>  8)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
    return x;
}

glm::uint32 morton_decode_x(glm::uint32 code)
{
    return morton_encode_by_two(code >> 0);
}

glm::uint32 morton_decode_y(glm::uint32 code)
{
    return morton_encode_by_two(code >> 1);
}

glm::uint32 morton_decode_z(glm::uint32 code)
{
    return morton_encode_by_two(code >> 2);
}

// "Insert" two 0 bits after each of the 10 low bits of x
glm::uint32 part_by_two(glm::uint32 x)
{
    x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
    x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x <<  8)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x <<  4)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x <<  2)) & 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    return x;
}

glm::uint32 morton_encode_3D(glm::uint32 x, glm::uint32 y, glm::uint32 z)
{
    return (part_by_two(z) << 2) + (part_by_two(y) << 1) + part_by_two(x);
}

glm::ivec3 morton_decode_3D(glm::uint32 code)
{
    return {morton_decode_x(code), morton_decode_y(code), morton_decode_z(code)};
}


void vvv::PassCompression::generate_brick_order() {

    static constexpr int curve_variants = 1;
    static constexpr bool permute_variants = false;

    // TODO: use multiple of Reiner's mixed curves, hilbert, radial curves..
    size_t i = 0;
    m_brick_order.resize(m_brick_size * m_brick_size * m_brick_size * (permute_variants ? 6 : 1) * curve_variants); // indices within brick * x/y/z-permutations (6) * space filling curves

    // permutation identifier
    glm::ivec3 permute(0);
    glm::ivec4 pos(0, 0, 0, 1.f);

    for(permute.z = 0; permute.z < 3; permute.z++) {
        for(permute.y = 0; permute.y < 3; permute.y++) {
            for (permute.x = 0; permute.x < 3; permute.x++) {
                // discard invalid permutations of x/y/z
                if(permute.y == permute.x || permute.z == permute.y || permute.z == permute.x)
                    continue;
                if(!permute_variants && (permute.x != 0 || permute.y != 1 || permute.z != 2))
                    continue;

                // naive / cartesian / line by line
                for(pos[permute.z] = 0; pos[permute.z] < m_brick_size; pos[permute.z]++) {
                    for(pos[permute.y] = 0; pos[permute.y] < m_brick_size; pos[permute.y]++) {
                        for(pos[permute.x] = 0; pos[permute.x] < m_brick_size; pos[permute.x]++) {
                            m_brick_order[i++] = pos;
                        }
                    }
                }

//                // hilbert
//                for(const auto& h: hilbert8x8x8) {
//                    pos.x = h[permute.x];
//                    pos.y = h[permute.y];
//                    pos.z = h[permute.z];
//                    m_brick_order[i++] = pos;
//                }

//                // morton
//                for(int curve_idx = 0; curve_idx < m_brick_size * m_brick_size * m_brick_size; curve_idx++) {
//                    pos.x = morton_decode_3D(curve_idx)[permute.x];
//                    pos.y = morton_decode_3D(curve_idx)[permute.y];
//                    pos.z = morton_decode_3D(curve_idx)[permute.z];
//                    m_brick_order[i++] = pos;
//                }

                assert(i <= m_brick_order.size() && "writing brick order offsets outside of order array");
            }
        }
    }

}

void vvv::PassCompression::init(std::string path, int brick_size) {
    assert(isPipelineCreated() && "you must call allocateResources() before initializing the compression parameters!");
    assert(m_path.empty() && "compression was initialized already!");
    assert(!path.empty() && brick_size > 0 && brick_size <= 32);
    m_path = path;
    m_brick_size = brick_size;

    // TODO: gather information about the data set to compress
    m_volume_frames = frames_of_dataset(path);
    auto volume = Volume<uint16_t>::load_volcanite_raw(frame_path(path, 0));
    m_volume_dim.x = volume->dim_x;
    m_volume_dim.y = volume->dim_y;
    m_volume_dim.z = volume->dim_z;
    Logger(ERROR) << "Initializing compression for " + m_path + " with " << m_volume_frames << " frames and dimensions " << str(m_volume_dim) << ", brick size " << m_brick_size;
    m_brick_dim = glm::uvec3(glm::ceil(glm::vec3(m_volume_dim) / static_cast<float>(m_brick_size)));

    // generate the order in which brick elements are indexed
    generate_brick_order();
    m_gpu.order_buffer = std::make_shared<Buffer>(getCtx(), BufferSettings{.label = "PassCompression.m_gpu.order_buffer", .byteSize = m_brick_order.size() * sizeof(glm::ivec4), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst});

    // reflect/create all resources
    vvv::AwaitableList reinitDone;
    const auto texVol = reflectTexture("IMAGE_volume", {.width = m_volume_dim.x, .height=m_volume_dim.x, .depth=volume->dim_z, .format = volume->format, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, .queues = vvv::TextureExclusiveQueueUsage});
    texVol->ensureResources();

    // note: (signed) integer indices can reference enough sequence buffer entries for 250 (frames) * 160*160*160 voxels * 2 rle integers without any compression
    m_gpu.sequence_buffer_size = 1024*1024*1024 / 4; // 2 GB of GPU memory for 512^3 storing int, should be enough for one frame
    m_gpu.sequence_buffer = std::make_shared<Buffer>(getCtx(), BufferSettings{.label = "PassCompression.m_gpu.sequence_buffer", .byteSize = m_gpu.sequence_buffer_size * sizeof(glm::int32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_gpu.index_buffer_size =  m_volume_frames * m_brick_dim.x * m_brick_dim.y * m_brick_dim.z; // 0.5 GB for 250 frames with 64^3 bricks storing ivec2
    m_gpu.index_buffer = std::make_shared<Buffer>(getCtx(), BufferSettings{.label = "PassCompression.m_gpu.index_buffer", .byteSize = m_gpu.index_buffer_size * sizeof(glm::ivec2), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_gpu.parameters = getUniformSet("compression_options");
    m_gpu.vol_img = reflectTexture("IMAGE_volume", {.width = m_volume_dim.x, .height=m_volume_dim.y, .depth=m_volume_dim.z, .format = volume->format, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, .queues = vvv::TextureExclusiveQueueUsage});
    m_gpu.verify_buffer = std::make_shared<Buffer>(getCtx(), BufferSettings{.label = "PassCompression.m_gpu.verify_buffer", .byteSize = sizeof(CompressionVerifyErrors), .usage = vk::BufferUsageFlagBits::eStorageBuffer});

    // create the result buffers
    m_sequence_buffer.resize((2 + m_volume_frames/4) * 2 * m_volume_dim.x * m_volume_dim.y * m_volume_dim.z); // TODO we won't need that much space!
    m_index_buffer.resize(m_volume_frames * m_brick_dim.x * m_brick_dim.y * m_brick_dim.z);
}




vvv::AwaitableHandle vvv::PassCompression::execute(vvv::AwaitableList awaitBeforeExecution, vvv::BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    assert(isPipelineCreated() && "you MUST call 'allocateResources' if the pass was created with lazy state initialization.");
    assert(!m_path.empty() && "you MUST call 'init' and set a correct path before execution.");

    GpuContextPtr ctx = getCtx();

    // we could make commandBuffer/queue optional and just use the default queue (family index 0)
    // that we get from the GpuContext. This is safe and simplifies the API when we only work on a single thread and do not care
    // about async compute or high performance transfers
    // updateResources(getActiveIndex()); // update pointer handles
    // updateUniformBufferMemory(getActiveIndex());
    auto &commandBuffer = m_commandBuffer->at(0);

    //**********************************************************************************************************************************************//
    //  STEP 1      SEQUENCE GENERATION
    //  Construct a) a buffer containing all sequences from all frames
    //            b) the index buffer containing the first memory index of the sequence in the sequencebuffer
    //               (may later contain additional info per sequence - selected index scheme, palette, dimension in case of multi-size bricking ...)
    //**********************************************************************************************************************************************//
    Logger(DEBUG, true) << "1. RLE sequence generation compression " << 0.f << "%";
    MiniTimer timer;

    // bind our buffers
    setStorageBuffer(0, 0, *m_gpu.sequence_buffer);
    setStorageBuffer(0, 1, *m_gpu.index_buffer);
    m_gpu.order_buffer->upload(m_brick_order);
    setStorageBuffer(0, 3, *m_gpu.order_buffer);
    setStorageImage("IMAGE_volume", *m_gpu.vol_img);

    // TODO create sequence_extraction shader
    const size_t bricks_per_frame = m_brick_dim.x * m_brick_dim.y * m_brick_dim.z;
    const size_t max_voxels_for_all_bricks = bricks_per_frame * m_brick_size * m_brick_size * m_brick_size;
    size_t top_of_sequence_buffer = 0;
    for(int frame=0; frame < m_volume_frames; frame++) {
        // TODO make it so frames can be dispatched one after the other, with the start indices within the buffers depending on the number of frames. (just an 'offset' uniform for index and sequence buffer in the shader)
        // this means: all threads get enough memory for the worst case sequence (2x original memory)

        // update uniform
        m_gpu.parameters->setUniform("frame", frame);
        m_gpu.parameters->setUniform("brick_size", m_brick_size);
        m_gpu.parameters->setUniform("brick_dim", glm::ivec3(m_brick_dim));
        m_gpu.parameters->setUniform("order_variants", static_cast<int32_t>(m_brick_order.size() / m_brick_size / m_brick_size / m_brick_size));
        m_gpu.parameters->upload();

        // load the next frame as volume
        std::string path_for_current_frame = frame_path(m_path, frame);
        auto volume = Volume<uint16_t>::load_volcanite_raw(path_for_current_frame);

        // update / upload shader data
        auto [volUploadFinished, _stagingBuffer] = m_gpu.vol_img->upload(volume->data());
        awaitBeforeExecution.push_back(volUploadFinished);
        awaitBeforeExecution.push_back(m_gpu.vol_img->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands));

        // generate sequences for all bricks
        executeCommands(commandBuffer, SEQUENCE_GEN, {m_brick_dim.x, m_brick_dim.y, m_brick_dim.z} );
        auto submission = getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);

        //**********************************************************************************************************************************************//
        //  STEP 1b     RLE BUFFER CLEANUP
        // "squeeze" the last downloaded part of the rle_buffer on the CPU and update the relevant parts of the index_buffer accordingly
        // we do this here because now the new GPU work is already submitted, and we can do CPU / GPU in parallel. The last frame will be squeezed after the loop.
        if(frame > 0)
            squeeze_sequence_buffer(top_of_sequence_buffer, frame-1, bricks_per_frame);

        // wait until compute finished to download the buffers to the top of our CPU sequence buffer
        size_t reserved_sequence_buffer_size = top_of_sequence_buffer + 2 * m_brick_dim.x * m_brick_dim.y * m_brick_dim.z * m_brick_size * m_brick_size * m_brick_size;
        if(m_sequence_buffer.size() < reserved_sequence_buffer_size) { // ToDo resize a bit more, to prevent resizing every frame
            Logger(WARN) << "resizing sequence buffer to hold " << reserved_sequence_buffer_size << " elements.";
            assert(reserved_sequence_buffer_size < 1000000000 && "sequence_buffer becomes too big (> 4 GB)");
            m_sequence_buffer.resize(reserved_sequence_buffer_size);
        }
        getCtx()->sync->hostWaitOnDevice({submission});
        // download the sequence buffer (the gpu buffer only contains the current frame, so we don't have to account for any device offset)
        m_gpu.sequence_buffer->download(&m_sequence_buffer[top_of_sequence_buffer], sizeof(int32_t) * 2 * max_voxels_for_all_bricks);
        // download the index buffer (the index buffer is complete on the GPU side, so we only want to download the new part for this frame by offsetting)
        m_gpu.index_buffer->download(&m_index_buffer[frame * bricks_per_frame], frame * sizeof(glm::ivec2) * bricks_per_frame, sizeof(glm::ivec2) * bricks_per_frame);

//        Logger(WARN) << " now check one block:";
//        {
//            int val;
//            int count;
//            int sequence_i = m_index_buffer[m_brick_dim.x - 1 + (m_brick_dim.y - 1) * m_brick_dim.x + (m_brick_dim.z - 1) * m_brick_dim.x * m_brick_dim.y].x;
//
//            // iterate through rle buffer
//            int voxel_max = m_brick_size * m_brick_size * m_brick_size;
//            int v = 0;
//            while (v < voxel_max) {
//
//                // read next entry from rle sequence buffer
//                count = m_sequence_buffer[sequence_i++];
//                val = m_sequence_buffer[sequence_i++];
//
//                std::cout << count << "x" << val << ": ";
//
//                for (int j = 0; j < count; j++) {
//
//                    glm::ivec3 voxel = glm::ivec3(m_brick_order[v]);
//
//                    if (glm::all(glm::lessThan(voxel, glm::ivec3(m_volume_dim)))) {
//                        std::cout << volume->getElement(voxel);
//                    }
//                    else {
//                        std::cout << "(" << volume->getElement(voxel) << ")";
//                    }
//
//                    v++;
//                }
//
//                std::cout << std::endl;
//            }
//        }


//        for(int i=0; i<10; i++)
//            Logger(ERROR) << m_sequence_buffer[i*2] << "x " << m_sequence_buffer[i*2+1];


        // log progress
        Logger(DEBUG, frame < m_volume_frames-1) << "1. RLE sequence generation compression " << (static_cast<float>(frame+1)/static_cast<float>(m_volume_frames)*100.f) << "%";
    }

    // finalize sequence and index buffer including one last squeeze for the last frame
    squeeze_sequence_buffer(top_of_sequence_buffer, m_volume_frames-1, bricks_per_frame);
    m_sequence_buffer.resize(top_of_sequence_buffer);
    Logger(DEBUG) << "Simple RLE encoding used " << top_of_sequence_buffer << " integers. Compression rate "
                  << (static_cast<float>(top_of_sequence_buffer)/static_cast<float>(m_volume_frames * m_volume_dim.x * m_volume_dim.y * m_volume_dim.z) * 100.f)
                  << "% Compression time " << timer.elapsed() << "s";


    // AT THIS POINT: we have buffer containing all RLE compressed sequences and an index buffer

    // TODO SSBO Memory Barrier needed? We probably have the un-eliminated RLE buffer in CPU memory now. Maybe we will just stream that to the GPU part-wise during all next steps
//    VkMemoryBarrier2KHR memoryBarrier = {
//        ...
//            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
//        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
//        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
//        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR };
//
//    VkDependencyInfoKHR dependencyInfo = {
//        ...
//        1,              // memoryBarrierCount
//        &memoryBarrier, // pMemoryBarriers
//        ...
//    }
//
//    vkCmdPipelineBarrier2KHR(commandBuffer, &dependencyInfo);

    // note: from now on we don't really care about different "frames". ALL sequences lie back to back in one buffer and the index texture contains ALL per-sequence information of all frames.

    //**********************************************************************************************************************************************//
    //  STEP 2      FIND MAXIMUM SHARED POSTFIX LENGTH OF ALL SEQUENCES
    //  for each seqeunce, determine the length Lmax of it's longest postfix, that is shared with another sequence. (using a hash map to find identical sequences)
    //**********************************************************************************************************************************************//


    //**********************************************************************************************************************************************//
    //  STEP 3      SET REPRESENTATIVES FOR ALL POSTFIX HASHES
    //  all sequences write their index and Lmax to the hashmap if their Lmax is greater than the current entry, or if it's equal and their index is lower than the current one.
    //  this way, the hashmap now stores the index of all representatives for the given postfix hashes and these are the ones with the longest shared postfix.
    //**********************************************************************************************************************************************//

    //**********************************************************************************************************************************************//
    //  STEP 4      POSTFIX ELIMINATION
    //  all sequences search for their longest postfix with an entry in the hashmap.
    //  If this representative is a different sequence we replace the rest of our sequence entries with a pointer to the position in the representative.
    //**********************************************************************************************************************************************//

    //**********************************************************************************************************************************************//
    //  STEP 5       BUFFER CLEANUP
    //  Copy the buffers so that everything is stored back to back and without empty / unused buffer regions for the sequences.
    //  Update the index buffer along the way too!
    //  TODO (we actually may have to do that way earlier so that everything fits into GPU memory during our computations hehe)
    //**********************************************************************************************************************************************//


    return nullptr;
}

vvv::AwaitableHandle vvv::PassCompression::verify(vvv::AwaitableList awaitBeforeExecution, vvv::BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    assert(isPipelineCreated() && "you MUST call 'allocateResources' if the pass was created with lazy state initialization.");
    assert(!m_path.empty() && "you MUST call 'init' and set a correct path before execution.");

    GpuContextPtr ctx = getCtx();

    // we could make commandBuffer/queue optional and just use the default queue (family index 0)
    // that we get from the GpuContext. This is safe and simplifies the API when we only work on a single thread and do not care
    // about async compute or high performance transfers
    // updateResources(getActiveIndex()); // update pointer handles
    // updateUniformBufferMemory(getActiveIndex());
    auto &commandBuffer = m_commandBuffer->at(0);

    //**********************************************************************************************************************************************//
    //  STEP 1      SEQUENCE GENERATION
    //  Construct a) a buffer containing all sequences from all frames
    //            b) the index buffer containing the first memory index of the sequence in the sequencebuffer
    //               (may later contain additional info per sequence - selected index scheme, palette, dimension in case of multi-size bricking ...)
    //**********************************************************************************************************************************************//

    // bind our buffers
    setStorageBuffer(0, 0, *m_gpu.sequence_buffer);
    setStorageBuffer(0, 1, *m_gpu.index_buffer);
    setStorageBuffer(0, 3, *m_gpu.order_buffer);
    setStorageImage("IMAGE_volume", *m_gpu.vol_img);

    CompressionVerifyErrors verify_result;
    setStorageBuffer(1, 5, *m_gpu.verify_buffer);

    // upload buffers
    m_gpu.sequence_buffer->upload(m_sequence_buffer);
    m_gpu.index_buffer->upload(m_index_buffer);


    // TODO create sequence_extraction shader
    for(int frame=0; frame < m_volume_frames; frame++) {
        memset(&verify_result, 0, sizeof(CompressionVerifyErrors));
        m_gpu.verify_buffer->upload(&verify_result, sizeof(CompressionVerifyErrors));

        // update uniform
        m_gpu.parameters->setUniform("frame", frame);
        m_gpu.parameters->setUniform("brick_size", m_brick_size);
        m_gpu.parameters->setUniform("brick_dim", glm::ivec3(m_brick_dim));
        m_gpu.parameters->setUniform("order_variants", static_cast<int32_t>(m_brick_order.size() / m_brick_size / m_brick_size / m_brick_size));
        m_gpu.parameters->upload();

        // load the next frame as volume
        std::string path_for_current_frame = frame_path(m_path, frame);
        auto volume = Volume<uint16_t>::load_volcanite_raw(path_for_current_frame);

        // update / upload shader data
        auto [volUploadFinished, _stagingBuffer] = m_gpu.vol_img->upload(volume->data());
        awaitBeforeExecution.push_back(volUploadFinished);
        awaitBeforeExecution.push_back(m_gpu.vol_img->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands));

        // generate sequences for all bricks
        executeCommands(commandBuffer, VERIFY, {m_brick_dim.x, m_brick_dim.y, m_brick_dim.z} );
        auto submission = getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);

        getCtx()->sync->hostWaitOnDevice({submission});

        m_gpu.verify_buffer->download(&verify_result, sizeof(CompressionVerifyErrors));
        if(glm::any(glm::notEqual(verify_result, CompressionVerifyErrors(0))))
            Logger(WARN) << "Verifying frame " << frame << "/" << (m_volume_frames - 1) << " with " << str(verify_result) << " errors.";
        else
            Logger(DEBUG) << "Verifying frame " << frame << "/" << (m_volume_frames - 1) << " with " << str(verify_result) << " errors.";
    }

    return nullptr;
}


void vvv::PassCompression::squeeze_sequence_buffer(size_t& top_of_sequence_buffer, const int frame, const size_t bricks_per_frame) {
    //TODO: change frame and bricks_per_frame to 'first_brick_id' and 'last_brick_id' to allow arbitrary regions instead of whole frames
    size_t entry_read_from = top_of_sequence_buffer;
    for(size_t b = frame * bricks_per_frame; b < (frame + 1) * bricks_per_frame; b++) {
        // save the correct start value
        int entry_length = m_index_buffer[b].x; // compute shader only stored the length of the whole entry. we replace it with the start location.
        assert(entry_length > 0 && entry_length <= 2 * m_brick_size * m_brick_size * m_brick_size);
        assert(b < m_index_buffer.size());
        assert(top_of_sequence_buffer < m_sequence_buffer.size());
        m_index_buffer[b].x = top_of_sequence_buffer;

        if(top_of_sequence_buffer != entry_read_from)
            memcpy(&m_sequence_buffer[top_of_sequence_buffer], &m_sequence_buffer[entry_read_from], sizeof(int32_t) * entry_length);

        assert(m_sequence_buffer[entry_read_from] != 0);
        assert(m_sequence_buffer[top_of_sequence_buffer] != 0);
        // skip read and write to the next brick
        top_of_sequence_buffer += entry_length;
        entry_read_from += m_brick_size * m_brick_size * m_brick_size * 2;
    }

}
