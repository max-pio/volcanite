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

#ifndef CSGV_UTILS_GLSL
#define CSGV_UTILS_GLSL

#include "cpp_glsl_include/csgv_constants.incl"

#define SIZEOF(Type) (uint64_t(Type(uint64_t(0))+1))

// Memory Access and Indexing Utilities --------------------------------------------------------------------------------
uint brick_pos2idx(const uvec3 brick_idx, const uvec3 brick_count) {
    return brick_idx.x + brick_count.x * (brick_idx.y + brick_count.y * brick_idx.z);
}

uvec3 brick_idx2pos(const uint brick_idx, const uvec3 brick_count) {
    assert(brick_idx < brick_count.x * brick_count.y * brick_count.z, "brick_idx out of bounds");
    return uvec3(brick_idx % brick_count.x,
                 (brick_idx / brick_count.x) % brick_count.y,
                 (brick_idx / brick_count.x / brick_count.y) % brick_count.z);
}

// adds the element offset (one unit = 4 byte) to the 64 bit address represented in an uvec2
uvec2 bufferAddressAdd(uvec2 address, const uint uint_elem_offset) {
    uint carry;
    // the offset is measured in uints but we have to add 4 byte per uint. To prevent uint overflow, we repeat the op:
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    address.x = uaddCarry(address.x, uint_elem_offset, carry); address.y += carry;
    return address;
}

// substracts the element offset (one unit = 4 byte) from the 64 bit address represented in an uvec2
uvec2 bufferAddressSub(uvec2 address, const uint uint_elem_offset) {
    uint borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    address.x = usubBorrow(address.x, uint_elem_offset, borrow); address.y -= borrow;
    return address;
}

uint getBrickStart(uint brick_idx) {
    if(g_brick_starts[brick_idx] > g_brick_starts[brick_idx + 1u])
        return 0u;
    else
        return g_brick_starts[brick_idx];
}

uint getBrickEnd(uint brick_idx) {
    return g_brick_starts[brick_idx + 1u];
}

uint getBrickEncodingLength(uint brick_idx) {
    return getBrickEnd(brick_idx) - getBrickStart(brick_idx);
}

EncodingRef getBrickEncodingRef(uint brick_idx) {
    return EncodingRef(bufferAddressAdd(g_encoding_buffer_addresses[brick_idx / g_brick_idx_to_enc_vector], getBrickStart(brick_idx)));
}

uint getBrickPaletteLength(uint brick_idx) {
    return getBrickEncodingRef(brick_idx).buf[PALETTE_SIZE_HEADER_INDEX];
}

#ifdef SEPARATE_DETAIL
EncodingRef getBrickDetailEncodingRef(uint brick_idx) {
    return EncodingRef(bufferAddressAdd(g_detail_buffer_address, g_detail_starts[brick_idx]));
}
#endif

#endif // CSGV_UTILS_GLSL
