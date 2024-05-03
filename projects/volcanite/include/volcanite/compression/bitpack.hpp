#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace vvv {

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const uint32_t* v, size_t start, size_t entry_id);

/**
 * Returns the entry_id-th 4 bit entry from the memory block starting at v[start].
 * Note that start counts in elements of size uint32_t and entry_id counts in elements of 4 bit size.
 */
uint32_t read4Bit(const std::vector<uint32_t> &v, size_t start, size_t entry_id);


/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(uint32_t* v, size_t start, size_t entry_id, uint32_t value4bit);

/**
 * Writes the value value4bit to the entry_id-th position in v, offset by start uint32_t elements.
 */
void write4Bit(std::vector<uint32_t> &v, size_t start, size_t entry_id, uint32_t value4bit);


/**
 * Squeezes 4bit elements into one uint32_t element respectively starting with first up to the element before last.
 * If the number of elements is not divided evenly, the last resulting element does not contain all entries, i.e. is padded.
 * @return the number of 4bit elements that were squeezed
 */
uint32_t pack4Bit(std::vector<uint32_t> &v, size_t start, size_t end);


std::vector<uint8_t> convertPacked32bit2PackedByte(const std::vector<uint32_t> &v);

std::vector<uint8_t> unpackHalfByte2Byte(std::vector<uint8_t> &v);


} // namespace vvv
