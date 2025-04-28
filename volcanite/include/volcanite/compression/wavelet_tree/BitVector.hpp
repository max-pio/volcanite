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
//
// This class is partially based on code from the pasta-toolkit BitVector implementation by Florian Kurpicz which is
// licensed under the GPLv3 license. https://github.com/pasta-toolbox/bit_vector

#pragma once

#include <bit>
#include <cassert>
#include <glm/integer.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace volcanite {

// Makes some of the GLSL functions available:

inline uint32_t bitfieldInsert(const uint32_t base, const uint32_t insert, const int offset, const int bits) {
    return glm::bitfieldInsert(base, insert, offset, bits);
}
inline uint64_t bitfieldInsert(const uint64_t base, const uint64_t insert, const int offset, const int bits) {
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
inline uint32_t bitCount(const uint32_t value) {
    return std::popcount(value);
}
inline uint32_t bitCount(const uint64_t value) {
    return std::popcount(value);
}

/// Words are the bit vector atomic storage unit and store bits in reverse order.
typedef uint64_t BV_WordType;
/// Bits covered by one word.
static constexpr uint32_t BV_WORD_BIT_SIZE = sizeof(BV_WordType) * 8u;

/// A bitvector implementation for wavelet matrices that is close to a C- or GLSL-style implementation.
/// It supports the rank0, rank1 and access operations.
/// Open question: could we use uvec4 as base elements?
class BitVector {

    // Some notes on optimizations:
    // Within a word, the bits are stored in reverse order, i.e. the first bit is the LSB. This saves one subtraction
    //   before computing the shift for the access operation: 63 62 ... 1 0 | 127 126 ... 65 64 | ...
    // In theory, >> 6 would be faster than dividing by a BIT_WORD_SIZE of 64. But the compiler optimizes this for us.
    // In theory, & 0b111111 would be faster than computing modulo by a BIT_WORD_SIZE of 64. But the compiler
    //   optimizes this for us. Therefore, we use the / and % notation for better readability.
    // Currently, the bit vector uses 32 bit indexing for bits meaning that at most 2^32 bits can be stored.
    // Using rank1 as the basic rank operation is faster, as it can directly use the popcount operation (+ shifts).

  public:
    BitVector() : m_size(0u), m_data() {}
    explicit BitVector(const std::vector<bool> &boolVector) : m_size(boolVector.size()),
                                                     m_data(words_for_size(boolVector.size())) {
        for (uint32_t i = 0; i < boolVector.size(); i++)
            set(i, boolVector[i]);
    }
    explicit BitVector(const uint32_t size) : m_size(size), m_data(words_for_size(size)) {}

    BitVector(const uint32_t size, const uint8_t bit) : m_size(size),
                                            m_data(words_for_size(size), bit ? ~0ull : 0ull) {}

    [[nodiscard]] uint8_t access(const uint32_t index) const {
        assert(index / BV_WORD_BIT_SIZE < m_data.size() && "bit vector access out of bounds");
        return bitfieldExtract(m_data[index / BV_WORD_BIT_SIZE], static_cast<int>(index % BV_WORD_BIT_SIZE), 1);
    }

    void set(const uint32_t index, const uint8_t bit_value) {
        assert(index < m_size && "trying to set bit in bit vector that is out of bounds.");
        assert(bit_value <= 1u && "bit_value must be 0 or 1.");

        // https://graphics.stanford.edu/~seander/bithacks.html#ConditionalSetOrClearBitsWithoutBranching
        // the compiler optimizes modulo and division into bit shift instructions (which require fewer cycles)
        BV_WordType mask = 1ull << (index % BV_WORD_BIT_SIZE);
        m_data[index / BV_WORD_BIT_SIZE] = (m_data[index / BV_WORD_BIT_SIZE] & ~mask) | (-static_cast<BV_WordType>(bit_value) & mask);
    }

    /// Resizes the vector so that it stores size many bits.
    void resize(const uint32_t size) {
        m_size = size;
        m_data.resize(words_for_size(size));
    }

    /// Reserves space for size many bits in memory without altering the bit vectors actual size.
    void reserve(const uint32_t size) {
        m_data.resize(words_for_size(m_size));
    }

    /// Removes all unused memory space if capacity() is greater than size().
    /// Note that this is a non-binding request to the underlying BVWordType std::vector.
    void shrink_to_fit() {
        const uint32_t target_size = words_for_size(m_size);
        if (capacity() > target_size)
            m_data.resize(target_size);
        m_data.shrink_to_fit();
    }

    /// Appends bit_value to the end of the bit vector. If this requires a capacity increase, the bit vector's current
    /// capacity is doubled.
    /// @param bit_value the bit value to append to the vector. Must be either 0 or 1.
    void push_back(const uint8_t bit_value) {
        m_size++;
        if (m_size > capacity())
            reserve(m_size * 2u);
        set(m_size - 1, bit_value);
    }

    [[nodiscard]] uint32_t size() const { return m_size; }
    [[nodiscard]] uint32_t capacity() const { return m_data.size() * BV_WORD_BIT_SIZE; }

    [[nodiscard]] std::string str() const {
        std::stringstream ss;
        for (uint32_t i = 0; i < m_size; i++) {
            ss << (access(i) ? '1' : '0');
            if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 1u && i < m_size - 1u)
                ss << " ";
        }
        return ss.str();
    }

    [[nodiscard]] const BV_WordType *getRawDataConst() const { return &m_data[0]; }
    [[nodiscard]] BV_WordType *getRawData() { return &m_data[0]; }
    [[nodiscard]] uint32_t getRawDataSize() const { return words_for_size(m_size); }

  private:
    /// @return how many BVWordType entries are needed to store size many bits.
    static uint32_t words_for_size(const uint32_t size) { return (size + BV_WORD_BIT_SIZE - 1u) / BV_WORD_BIT_SIZE; }

    uint32_t m_size;                 ///< number of bits stored in the bit vector
    std::vector<BV_WordType> m_data; ///< the raw data array storing bits in BVWordType words
};

// RANK ACCELERATION STRUCTURE -----------------------------------------------------------------------------------------

// To stick to the naming conventions, some of the following constants and class names are taken from pasta::bit_vector
// (C) 2021 Florian Kurpicz <florian@kurpicz.org>, released under the GPLv3 license:
// https://github.com/pasta-toolbox/bit_vector

/// Atomic type that stores one L1-block. Default configuration: The L1-block contains 6 L2-blocks.
/// The 19 LSB store the L1-information. Followed by 5 L2-information (first is implicit 0) ordered from least to most
/// significant bits using 9 bits each. 19 bits + 5 * 9 bits = 64 bits total. This is enough to address vectors with
/// 64³ + 32³ + 16³ + 8³ + 4³ + 2³ + 1³ bit entries, i.e. the maximum possible number of operations in a 64³ CSGB brick.
typedef uint64_t BV_L12Type;

struct FlatRank_BitVector_ptrs {
    const BV_L12Type *fr;
    const BV_WordType *bv;
};

/// Number of L2-blocks that are grouped into one L1-block MINUS ONE. The first L2-block is not stored explicitly.
static constexpr uint32_t BV_STORE_L2_PER_L1 = 4;
/// Bits that each stored L1-block takes up in the BV_L12Type
static constexpr uint32_t BV_STORE_L1_BITS = 20;
/// Bits that each stored L2-block takes up in the BV_L12Type
static constexpr uint32_t BV_STORE_L2_BITS = 11;
/// Bits covered by an L2-block.
static constexpr uint32_t BV_L2_BIT_SIZE = 4 * BV_WORD_BIT_SIZE;

// some reasonable configurations:
// 5 19 9 1     (fastest, 16,67% overhead)
// 4 20 11 4    (2x as slow, 5% overhead)
// 3 19 12 8    (3x as slow, 3.125% overhead)

/// Bits covered by an L1-block.
static constexpr uint32_t BV_L1_BIT_SIZE = (BV_STORE_L2_PER_L1 + 1) * BV_L2_BIT_SIZE;
/// Number of 64-bit words covered by an L2-block.
static constexpr uint32_t BV_L2_WORD_SIZE = BV_L2_BIT_SIZE / BV_WORD_BIT_SIZE;
/// Number of 64-bit words covered by an L1-block.
static constexpr uint32_t BV_L1_WORD_SIZE = BV_L1_BIT_SIZE / BV_WORD_BIT_SIZE;

// check if the configuration leads to any problems
static_assert(BV_L2_WORD_SIZE > 0u, "L1- and L2-blocks must cover at least one word.");
static_assert(BV_L1_WORD_SIZE > BV_L2_WORD_SIZE, "L1-blocks must cover more words than L2-blocks.");
static_assert((BV_L2_BIT_SIZE / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE == BV_L2_BIT_SIZE,
              "L2 bit size must be a multiple of the word bit size");
static_assert((BV_STORE_L2_PER_L1 * BV_STORE_L2_BITS) + BV_STORE_L1_BITS <= (sizeof(BV_L12Type) * 8u),
              "L12 type not big enough to store all bits for the L1 and L2 information.");
static_assert((1u << BV_STORE_L1_BITS) + (BV_STORE_L2_PER_L1 + 1u) * (1u << BV_STORE_L2_BITS) > 37449u,
              "L12 blocks cannot index the maximum possible number of operations in a 32³ brick.");
static_assert((1u << BV_STORE_L1_BITS) + (BV_STORE_L2_PER_L1 + 1u) * (1u << BV_STORE_L2_BITS) > 262144u,
              "L12 blocks cannot index the maximum possible number of operations in the finest 64³ LOD.");
static_assert((1u << BV_STORE_L1_BITS) + (BV_STORE_L2_PER_L1 + 1u) * (1u << BV_STORE_L2_BITS) > 299593u,
              "L12 blocks cannot index the maximum possible number of operations in a 64³ brick.");
static_assert((1u << BV_STORE_L2_BITS) > BV_STORE_L2_PER_L1 * BV_L2_WORD_SIZE * BV_WORD_BIT_SIZE,
              "L2 bit depth cannot index the maximum possible number of bits within an L1 block.");

inline uint32_t rank1Word(const BV_WordType value, const uint32_t index) {
    return index ? bitCount(value << (BV_WORD_BIT_SIZE - index)) : 0u;
}

inline uint32_t getL1Entry(const BV_L12Type &v) {
    return bitfieldExtract(v, 0, BV_STORE_L1_BITS); // the least significant BV_STORE_L1_BITS store the L1-information
}

inline uint32_t getL2Entry(const BV_L12Type &v, const uint32_t i) {
    // First L2-information is always zero and not stored explicitly. For i > 0, BV_STORE_L2_BITS bits are stored per
    // L2-information (e.g. 9 bits per for all except the first one L2-block each). They are ordered in the BV_L12Type
    // from LSB to MSB, starting after the least significant BV_STORE_L1_BITS bits (e.g. 19) that are used for L1-info.
    static constexpr uint32_t OFFSET = BV_STORE_L1_BITS - BV_STORE_L2_BITS;
    return i ? bitfieldExtract(v, static_cast<int>(i * BV_STORE_L2_BITS + OFFSET), BV_STORE_L2_BITS) : 0u;
}

inline BV_L12Type buildL12Type(const uint32_t l1, const uint32_t l2[BV_STORE_L2_PER_L1]) {
    // L1-information in LSB
    BV_L12Type entry = l1;
    assert(l1 < (1u << BV_STORE_L1_BITS) && "l1 value is too large to be stored in L12 block");
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

  public:
    explicit FlatRank(const BitVector &bv) {
        assert(bv.size() < maximumBitVectorSize() && "Bit Vector is too large for FlatRank construction.");

        // determine required number of L1-blocks and create array
        // store (up to) one more element than necessary to allow rank(text_size) queries
        m_size = bv.size() / BV_L1_BIT_SIZE + 1u;
        m_data = new BV_L12Type[m_size];

        m_bit_vector_data = bv.getRawDataConst();
        const uint32_t word_count = bv.getRawDataSize();

        uint32_t l1_entry = 0u;
        uint32_t l2_entries[BV_STORE_L2_PER_L1];

        // iterate through the bit vector word by word
        uint32_t word = 0u;
        // write the L12 entries from front to back
        uint32_t data_i = 0u;

        while (word < word_count) {
// gather values for all l2 entries
#pragma unroll
            for (uint32_t _l2 = 0u; _l2 < BV_STORE_L2_PER_L1; _l2++) {

                // L2-entries store the number of ones before each entry WITHIN the L1-block.
                // the first L2-entry would always be zero and thus skipped.
                l2_entries[_l2] = _l2 > 0u ? l2_entries[_l2 - 1u] : 0u;
// each L2-block covers 1 or more words
#pragma unroll(BV_L2_WORD_SIZE)
                for (uint32_t _w = 0u; _w < BV_L2_WORD_SIZE; _w++) {
                    if (word < word_count)
                        l2_entries[_l2] += bitCount(m_bit_vector_data[word++]);
                }
            }

            // write the data_i-th L12 entry
            m_data[data_i] = buildL12Type(l1_entry, l2_entries);
            data_i++;

            // update L1 tracking by adding bits of next (non-stored) L2-block
            l1_entry += l2_entries[BV_STORE_L2_PER_L1 - 1u];
#pragma unroll(BV_L2_WORD_SIZE)
            for (uint32_t _w = 0u; _w < BV_L2_WORD_SIZE; _w++) {
                if (word < word_count)
                    l1_entry += bitCount(m_bit_vector_data[word++]);
            }
        }

        // add one last dummy entry if the bit vector length is evenly dividable by the covered bit count
        // to support rank(text_size) queries.
        if (data_i < m_size)
            m_data[data_i] = buildL12Type(l1_entry, l2_entries);
    }

    ~FlatRank() {
        delete[] m_data;
        m_data = nullptr;
    }

    [[nodiscard]] const BV_L12Type *getRawData() const { return m_data; }
    [[nodiscard]] uint32_t getRawDataSize() const { return m_size; }

    [[nodiscard]] uint32_t rank0(const uint32_t index) const { return index - rank1(index); }

    /// @return the number of 1 bits in the bit vector that occur before index
    [[nodiscard]] uint32_t rank1(const uint32_t index) const {

        // ........ ........  bits
        // ┌┐┌┐┌┐┌┐ ┌┐┌┐┌┐┌┐  words
        // └┘└┘└┘└┘ └┘└┘└┘└┘
        // ┌──┐┌──┐ ┌──┐┌──┐  l2-blocks
        // └──┘└──┘ └──┘└──┘
        // ┌──────┐ ┌──────┐  l1-blocks
        // └──────┘ └──────┘

        // query L12 acceleration structure
        assert(index / BV_L1_BIT_SIZE < m_size && "accessing index out of flat rank range");
        BV_L12Type l12 = m_data[index / BV_L1_BIT_SIZE];
        uint32_t rank1_res = getL1Entry(l12);
        rank1_res += getL2Entry(l12, (index % BV_L1_BIT_SIZE) / BV_L2_BIT_SIZE);

        // perform bit counts on a word level to count the remaining bits
        uint32_t offset = ((index / BV_WORD_BIT_SIZE) / BV_L2_WORD_SIZE) * BV_L2_WORD_SIZE;
        // fill missing 'full' counted words if L2-blocks cover multiple words
        if (BV_L2_WORD_SIZE > 1) {
            for (uint32_t _w = 0u; _w < ((index / BV_WORD_BIT_SIZE) % BV_L2_WORD_SIZE); _w++) {
                rank1_res += bitCount(m_bit_vector_data[offset]);
                offset++;
            }
        }
        // if this is a rank(text_size) query, the inlining of the function lead to the potential out of bounds
        // access bv[offset] being ignored.
        return rank1_res + rank1Word(m_bit_vector_data[offset], index % BV_WORD_BIT_SIZE);
    }

    /// @return the overhead that this structure introduces relative to the size of its underlying bit vector
    static float overhead() { return static_cast<float>(sizeof(BV_L12Type) * 8u) / static_cast<float>(BV_L1_BIT_SIZE); }
    /// @return the maximum size (in bits) that the underlying bit vector of this structure can have
    static uint32_t maximumBitVectorSize() {
        return (1u << BV_STORE_L1_BITS) + (BV_STORE_L2_PER_L1 + 1u) * (1u << BV_STORE_L2_BITS) - 1u;
    }

  private:
    uint32_t m_size;                      ///< number of BV_L12Type entries stored, i.e. number of L1-blocks covering the bit vector
    BV_L12Type *m_data;                   ///< array of BV_L12Type entries storing the L1-blocks back to back
    const BV_WordType *m_bit_vector_data; ///< reference to the data array of the bit vector
};

} // namespace volcanite
