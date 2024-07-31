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
#include <glm/integer.hpp>

namespace volcanite {

// Makes some of the GLSL functions available:

inline uint32_t bitfieldInsert(uint32_t base, uint32_t insert, int offset, int bits) {
    return glm::bitfieldInsert(base, insert, offset, bits);
}
inline uint64_t bitfieldInsert(uint64_t base, uint64_t insert, int offset, int bits) {
    return glm::bitfieldInsert(base, insert, offset, bits);
}
inline uint32_t bitfieldExtract(uint32_t value, int offset, int bits) {
#ifdef __BMI__
    return _bextr_u32(value, offset, bits);
#else
    return (value >> offset) & ((1u << bits) - 1u);
#endif
}
inline uint64_t bitfieldExtract(uint64_t value, int offset, int bits) {
#ifdef __BMI__
    return _bextr_u64(value, offset, bits);
#else
    return (value >> offset) & ((1ull << bits) - 1u);
#endif
}
inline uint32_t bitCount(uint32_t value) {
    return std::popcount(value);
}
inline uint32_t bitCount(uint64_t value) {
    return std::popcount(value);
}

/// Words are the bit vector atomic storage unit and store bits in reverse order.
typedef uint64_t BV_WordType;
/// Bits covered by one word.
static constexpr uint32_t WORD_BIT_SIZE = sizeof(BV_WordType) * 8u;

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
       return bitfieldExtract(m_data[index / WORD_BIT_SIZE], static_cast<int>(index % WORD_BIT_SIZE), 1);
    }

    void set(uint32_t index, uint8_t bit_value) {
        assert(index < m_size && "trying to set bit in bit vector that is out of bounds.");
        assert(bit_value <= 1u && "bit_value must be 0 or 1.");

        // https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
        // the compiler optimizes modulo and division into bit shift instructions (which require fewer cycles)
        BV_WordType mask = 1ull << (index % WORD_BIT_SIZE);
        m_data[index / WORD_BIT_SIZE] = (m_data[index / WORD_BIT_SIZE] & ~mask) | (-static_cast<BV_WordType>(bit_value) & mask);
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

    [[nodiscard]] const BV_WordType* getRawData() const { return &m_data[0]; }
    [[nodiscard]] uint32_t getRawDataSize() const { return words_for_size(m_size); }

private:

    /// @return how many BVWordType entries are needed to store size many bits.
    static inline uint32_t words_for_size(uint32_t size) { return (size + WORD_BIT_SIZE - 1u) / WORD_BIT_SIZE; }

    uint32_t m_size;                    ///< number of bits stored in the bit vector
    std::vector<BV_WordType> m_data;     ///< the raw data array storing bits in BVWordType words

};


// RANK ACCELERATION STRUCTURE -----------------------------------------------------------------------------------------

// To stick to the naming conventions, some of the following constants and class names are taken from pasta::bit_vector
// (C) 2021 Florian Kurpicz <florian@kurpicz.org>, released under the GPLv3 license:
// https://github.com/pasta-toolbox/bit_vector

/// Atomic type that stores one L2-block consisting of 6 L1-blocks.
/// The 19 LSB store the L1-information. Followed by 5 L2-information (first is implicit 0) ordered from least to most
/// significant bits using 9 bits each. 19 bits + 5 * 9 bits = 64 bits total. This is enough to address vectors with
/// 64³ + 32³ + 16³ + 8³ + 4³ + 2³ + 1³ bit entries, i.e. the maximum possible number of operations in a 64³ CSGB brick.
typedef uint64_t BV_L12Type;

/// Number of L2-blocks that are grouped into one L1-block MINUS ONE. The first L2-block is not stored explicitly.
static constexpr uint32_t BV_STORE_L2_PER_L1 = 5;
/// Bits that each stored L1-block takes up in the BV_L12Type
static constexpr uint32_t BV_STORE_L1_BITS = 19;
/// Bits that each stored L2-block takes up in the BV_L12Type
static constexpr uint32_t BV_STORE_L2_BITS = 9;

/// Bits covered by an L2-block.
static constexpr uint32_t BV_L2_BIT_SIZE = 64;
/// Bits covered by an L1-block.
static constexpr uint32_t BV_L1_BIT_SIZE = (BV_STORE_L2_PER_L1 + 1) * BV_L2_BIT_SIZE;
/// Number of 64-bit words covered by an L2-block.
static constexpr uint32_t BV_L2_WORD_SIZE = BV_L2_BIT_SIZE / (sizeof(uint64_t) * 8);
/// Number of 64-bit words covered by an L1-block.
static constexpr uint32_t BV_L1_WORD_SIZE = BV_L1_BIT_SIZE / (sizeof(uint64_t) * 8);


inline uint32_t rank1(BV_WordType value, uint32_t index) {
    return bitCount(value << (WORD_BIT_SIZE - index));
}

inline uint32_t getL1Entry(const BV_L12Type& v) {
    return bitfieldExtract(v, 0, BV_STORE_L1_BITS); // the least significant BV_STORE_L1_BITS store the L1-information
}

inline uint32_t getL2Entry(const BV_L12Type& v, uint32_t i) {
    // First L2-information is always zero and not stored explicitly. For i > 0, BV_STORE_L2_BITS bits are stored per
    // L2-information (e.g. 9 bits per for all except the first one L2-block each). They are ordered in the BV_L12Type
    // from LSB to MSB, starting after the least significant BV_STORE_L1_BITS bits (e.g. 19) that are used for L1-info.
    static constexpr uint32_t OFFSET = BV_STORE_L1_BITS - BV_STORE_L2_BITS;
    return i == 0u ? 0u : bitfieldExtract(v, static_cast<int>(i * BV_STORE_L2_BITS + OFFSET), BV_STORE_L2_BITS);
}

inline BV_L12Type buildL12Type(uint32_t l1, const uint32_t l2[BV_STORE_L2_PER_L1]) {
    // L1-information in LSB
    BV_L12Type entry = l1;
    // followed by (BV_L2_PER_L1-1) entries for (non-implicit) L2-information
    #pragma unroll(BV_STORE_L2_PER_L1)
    for (uint32_t i = 0; i < (BV_STORE_L2_PER_L1); i++)
        entry |= static_cast<BV_L12Type>(l2[i]) << (BV_STORE_L1_BITS + i * BV_STORE_L2_BITS);
    return entry;
}

/// Two-level acceleration structure for rank queries on a (constant) bit vector. Note that this is only valid as long
/// as the bit vector does not change. The FlatRank structure cannot be updated. If the bit vector changes, you have
/// to recompute a new FlatRank - which is lightweight enough so that this does not introduce additional overhead.
class FlatRank {

    FlatRank(const BitVector& bv) {
        // determine required number of L1-blocks and create array
        m_size = (bv.size() + BV_L1_BIT_SIZE - 1u) / BV_L1_BIT_SIZE;
        m_data = new BV_L12Type[m_size];

        // iterate over bv from front to back and create L12 entries on the go

        uint32_t l1_entry = 0u;
        uint32_t l2_entries[BV_STORE_L2_PER_L1] = {0u, 0u, 0u, 0u, 0u};

        // TODO: continue from here
    }

    ~FlatRank() {
        delete[] m_data;
        m_data = nullptr;
    }

    BV_L12Type* getRawData() { return m_data; }
    uint32_t getRawDataSize() { return m_size; }


public:
    uint32_t rank1(uint32_t index) {
        return ~0u;
    }

private:
    uint32_t m_size;       ///< number of BV_L12Type entries stored, i.e. number of L1-blocks covering the bit vector
    BV_L12Type* m_data;    ///< array of BV_L12Type entries storing the L1-blocks back to back
};


}

