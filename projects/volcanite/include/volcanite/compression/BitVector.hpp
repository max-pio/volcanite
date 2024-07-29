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

#pragma once

#include <cstdint>
#include <bit>

namespace volcanite {


typedef uint32_t BitWordType;

inline uint32_t rank1(BitWordType value, uint32_t index) {
    return std::popcount(value << (32u - index));
}

inline uint32_t access(BitWordType value, uint32_t index) {
    return (value >> (32u - index)) & 1u;
}

inline void set

/// A bitvector implementation for wavelet matrices that is close to a C-style implementation.
/// It supports the rank0, rank1 and access operations on 32 bit unsigned integers.
/// This allows to easily port the code to GLSL shader code. Open question: would using uvec4 as base elements, or
/// uint64 as base elements have performance improvements?
class BitVector {


};

}

