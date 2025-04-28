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
#include <vector>

namespace volcanite {

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const uint32_t *v, std::size_t start, std::size_t entry_id);

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const std::vector<uint32_t> &v, std::size_t start, std::size_t entry_id);

/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(uint32_t *v, std::size_t start, std::size_t entry_id, uint32_t value4bit);

/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(std::vector<uint32_t> &v, std::size_t start, std::size_t entry_id, uint32_t value4bit);

/**
 * Squeezes 4bit elements into one uint32_t element respectively starting with first up to the element before last.
 * If the number of elements is not divided evenly, the last resulting element does not contain all entries, i.e. is padded.
 * @return the number of 4bit elements that were squeezed
 */
uint32_t pack4Bit(std::vector<uint32_t> &v, std::size_t start, std::size_t end);

std::vector<uint8_t> convertPacked32bit2PackedByte(const std::vector<uint32_t> &v);

std::vector<uint8_t> unpackHalfByte2Byte(std::vector<uint8_t> &v);

} // namespace volcanite
