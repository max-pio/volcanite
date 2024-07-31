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
#include <cassert>
#include <vector>
#include <string>
#include <sstream>

namespace volcanite {

/// Words are the bit vector atomic storage unit and store bits in reverse order.
typedef uint64_t BVWordType;
/// Bits covered by one word.
static constexpr uint32_t WORD_BIT_SIZE = sizeof(BVWordType) * 8u;

// To stick to the naming conventions, some of the following constants are taken from pasta::bit_vector:
// (C) 2021 Florian Kurpicz <florian@kurpicz.org>, released under the GPLv3 license
// https://github.com/pasta-toolbox/bit_vector

/// Bits covered by an L2-block.
static constexpr uint32_t L2_BIT_SIZE = 512;
/// Bits covered by an L1-block.
static constexpr uint32_t L1_BIT_SIZE = 8 * L2_BIT_SIZE;

/// Number of 64-bit words covered by an L2-block.
static constexpr uint32_t L2_WORD_SIZE = L2_BIT_SIZE / (sizeof(uint64_t) * 8);
/// Number of 64-bit words covered by an L1-block.
static constexpr uint32_t L1_WORD_SIZE = L1_BIT_SIZE / (sizeof(uint64_t) * 8);

inline uint32_t rank1(BVWordType value, uint32_t index) {
    return std::popcount(value << (WORD_BIT_SIZE - index));
}




/// A bitvector implementation for wavelet matrices that is close to a C- or GLSL-style implementation.
/// It supports the rank0, rank1 and access operations.
/// Open question: could we use uvec4 as base elements?
class BitVector {

    // Some notes on optimizations:
    // * within a word, the bits are stored in reverse order, i.e. the first bit is the LSB. This saves one subtraction
    //   before computing the shift for the access operation.
    // * in theory, >> 6 would be faster than dividing by a BIT_WORD_SIZE of 64. But the compiler optimizes this for us.
    // * in theory, & 0b111111 would be faster than computing modulo by a BIT_WORD_SIZE of 64. But the compiler
    //   optimizes this for us. Therefore, we use the / and % notation for better readability.

public:
    BitVector() : m_size(0u), m_data() {}
    BitVector(uint32_t size) : m_size(size), m_data(words_for_size(size)) {}

    BitVector(uint32_t size, uint8_t bit) : m_size(size),
                                            m_data(words_for_size(size), bit ? ~0ull : 0ull) {}

   [[nodiscard]] uint8_t access(uint32_t index) const {
       return (m_data[index / WORD_BIT_SIZE] >> index) & 1u;
    }

    void set(uint32_t index, uint8_t bit_value) {
        assert(index < m_size && "trying to set bit in bit vector that is out of bounds.");
        assert(bit_value <= 1u && "bit_value must be 0 or 1.");

        // https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
        // the compiler optimizes modulo and division into bit shift instructions (which require fewer cycles)
        BVWordType mask = 1ull << (index % WORD_BIT_SIZE);
        m_data[index / WORD_BIT_SIZE] = (m_data[index / WORD_BIT_SIZE] & ~mask) | (-static_cast<BVWordType>(bit_value) & mask);
    }

    /// Resizes the vector so that it stores size many bits.
    void resize(uint32_t size) {
        m_size = size;
        m_data.resize(words_for_size(size));
    }

    /// Reserves space for size many bits in memory without altering the bit vectors actual size.
    void reserve(uint32_t size) {
        m_data.resize(words_for_size(m_size));
    }

    /// Removes all unused memory space if capacity() is greater than size().
    /// Note that this is a non-binding request to the underlying BVWordType std::vector.
    void shrink_to_fit() {
        uint32_t target_size = words_for_size(m_size);
        if (capacity() > target_size)
            m_data.resize(target_size);
        m_data.shrink_to_fit();
    }

    /// Appends bit_value to the end of the bit vector. If this requires a capacity increase, the bit vector's current
    /// capacity is doubled.
    /// @param bit_value the bit value to append to the vector. Must be either 0 or 1.
    void push_back(uint8_t bit_value) {
        m_size++;
        if (m_size > capacity())
            reserve(m_size * 2u);
        set(m_size - 1, bit_value);
    }

    [[nodiscard]] uint32_t size() const { return m_size; }
    [[nodiscard]] uint32_t capacity() const { return m_data.size() * WORD_BIT_SIZE; }

    [[nodiscard]] std::string str() const {
        std::stringstream ss;
        for (uint32_t i = 0; i < m_size; i++) {
            ss << (access(i) ? '1' : '0');
            if (i % WORD_BIT_SIZE == WORD_BIT_SIZE - 1u && i < m_size - 1u)
                ss << " ";
        }
        return ss.str();
    }

    [[nodiscard]] const BVWordType* rawData() const { return &m_data[0]; }
    [[nodiscard]] uint32_t rawDataSize() const { return words_for_size(m_size); }

private:

    /// @return how many BVWordType entries are needed to store size many bits.
    static inline uint32_t words_for_size(uint32_t size) { return (size + WORD_BIT_SIZE - 1u) / WORD_BIT_SIZE; }

    uint32_t m_size;                    ///< number of bits stored in the bit vector
    std::vector<BVWordType> m_data;     ///< the raw data array storing bits in BVWordType words

};

}

