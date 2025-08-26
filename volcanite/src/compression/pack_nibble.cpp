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

#include "volcanite/compression/pack_nibble.hpp"

#include <cassert>

namespace volcanite {

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const uint32_t *v, const size_t start, const size_t entry_id) {
    // we packed into the highest bits first: one uint contains the indices [0000111122223333 ... 7777]
    // assert(start + entry_id / 8 < v.size() && "trying to unpack element out of bounds");
    const int shift = 28 - 4 * static_cast<int>(entry_id % 8);
    return (v[start + entry_id / 8] >> shift) & 0xFu;
    //    return (v[start + entry_id / 8] & (0xF0000000u >> shift)) >> (28 - shift); // first entry in lower bits
}

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const std::vector<uint32_t> &v, const size_t start, const size_t entry_id) {
    // we packed into the highest bits first: one uint contains the indices [0000111122223333 ... 7777]
    assert(start + entry_id / 8 < v.size() && "trying to unpack element out of bounds");
    const int shift = 28 - 4 * static_cast<int>(entry_id % 8);
    return (v[start + entry_id / 8] >> shift) & 0xFu;
}

/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(uint32_t *v, const std::size_t start, const std::size_t entry_id, const uint32_t value4bit) {
    assert(value4bit < 16 && "value must be within 4 bit range");

    const int shift = 28 - 4 * static_cast<int>(entry_id % 8);
    // erase bits at position
    v[start + entry_id / 8] &= ~(0xFu << shift);
    // add 4 bit bits to position
    v[start + entry_id / 8] |= value4bit << shift;
}

/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(std::vector<uint32_t> &v, const std::size_t start, const std::size_t entry_id, const uint32_t value4bit) {
    assert(value4bit < 16 && "value must be within 4 bit range");
    assert(start + entry_id / 8 < v.size() && "trying to write element out of bounds");

    const int shift = 28 - 4 * static_cast<int>(entry_id % 8);
    // erase bits at position
    v[start + entry_id / 8] &= ~(0xFu << shift);
    // add 4 bit bits to position
    v[start + entry_id / 8] |= value4bit << shift;
}

/**
 * Squeezes 4bit elements into one uint32_t element respectively starting with first up to the element before last.
 * If the number of elements is not divided evenly, the last resulting element does not contain all entries, i.e. is padded.
 * @return the number of 4bit elements that were squeezed
 */
uint32_t pack4Bit(std::vector<uint32_t> &v, const std::size_t start, const std::size_t end) {
    // iterate over all 32 bit elements, pack them into 4 bit elements in the same array
    std::size_t i = start;
    for (; i < end; i++) {
        write4Bit(v.data(), start, i, v[i]);
    }
    // round up: how many elements did we write divided by packing factor
    const std::size_t new_size = (i - start + 7) / 8;

    // resize the vector (this won't change the capacity / reserve a new array)
    v.erase(v.begin() + static_cast<long>(start + new_size), v.begin() + static_cast<long>(end));
    return new_size;
}

std::vector<uint8_t> convertPacked32bit2PackedByte(const std::vector<uint32_t> &v) {
    std::vector<uint8_t> out;
    for (const uint32_t x : v) {
        out.push_back(x >> 3 * 8);
        out.push_back(x >> 2 * 8);
        out.push_back(x >> 1 * 8);
        out.push_back(x >> 0 * 8);
    }
    assert(out.size() == v.size() * 4);
    return out;
}

std::vector<uint8_t> unpackHalfByte2Byte(const std::vector<uint8_t> &v) {
    std::vector<uint8_t> out(v.size() * 2);
    for (std::size_t i = 0; i < v.size(); i++) {
        out.at(i * 2) = (v[i] >> 4);
        out.at(i * 2 + 1) = (v[i] & 0xF);
    }
    return out;
}

} // namespace volcanite
