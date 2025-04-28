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
#include "volcanite/compression/wavelet_tree/BitVector.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"
#include "vvv/util/util.hpp"

#include <iomanip>
#include <iostream>
#include <random>
#include <string>

using namespace volcanite;

std::random_device rd;
std::mt19937 mt(rd());
std::uniform_real_distribution<double> dist(0., 1.);

#define STR_BITS_FRONT_BACK 64
#define STR_PLACEHOLDER " ...   "

std::string str(const std::vector<bool> &bv) {
    std::stringstream ss;
    bool wrote_dots = false;
    for (uint32_t i = 0; i < bv.size(); i++) {

        if (i >= STR_BITS_FRONT_BACK && i < (bv.size() / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE - STR_BITS_FRONT_BACK) {
            if (!wrote_dots) {
                ss << STR_PLACEHOLDER;
                wrote_dots = true;
            }
            continue;
        }

        ss << (bv[i] ? '1' : '0');
        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 1u && i < bv.size() - 1u)
            ss << " ";
    }
    return ss.str();
}

std::string str(const BitVector &bv) {
    bool wrote_dots = false;
    std::stringstream ss;
    for (uint32_t i = 0; i < bv.size(); i++) {
        if (i >= STR_BITS_FRONT_BACK && i < (bv.size() / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE - STR_BITS_FRONT_BACK) {
            if (!wrote_dots) {
                ss << STR_PLACEHOLDER;
                wrote_dots = true;
            }
            continue;
        }

        ss << (bv.access(i) ? '1' : '0');
        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 1u && i < bv.size() - 1u)
            ss << " ";
    }
    return ss.str();
}

std::string rankStrTicks(const BitVector &bv) {
    bool wrote_dots = false;
    std::stringstream ss;
    for (uint32_t i = 0; i < bv.size(); i += 4u) {
        if (i >= STR_BITS_FRONT_BACK && i < (bv.size() / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE - STR_BITS_FRONT_BACK) {
            if (!wrote_dots) {
                ss << STR_PLACEHOLDER;
                wrote_dots = true;
            }
            continue;
        }

        ss << "|   ";

        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 4u && i < bv.size() - 4u)
            ss << " ";
    }
    return ss.str();
}

std::string rankStrReference(const BitVector &bv) {
    bool wrote_dots = false;
    std::stringstream ss;
    ss << std::setfill(' ') << std::left;
    uint32_t ref_rank = 0u;
    for (uint32_t i = 0; i < bv.size(); i += 4u) {
        if (i >= STR_BITS_FRONT_BACK && i < (bv.size() / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE - STR_BITS_FRONT_BACK) {
            if (!wrote_dots) {
                ss << STR_PLACEHOLDER;
                wrote_dots = true;
            }

            for (uint32_t n = i; n < i + 4; n++)
                ref_rank += bv.access(n);
            continue;
        }

        ss << std::setw(4) << ref_rank;
        for (uint32_t n = i; n < i + 4; n++)
            ref_rank += bv.access(n);

        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 4u && i < bv.size() - 4u)
            ss << " ";
    }
    return ss.str();
}

std::string rankStrFlatRank(const FlatRank &f, const BitVector &bv) {
    bool wrote_dots = false;
    std::stringstream ss;
    ss << std::setfill(' ') << std::left;
    for (uint32_t i = 0; i < bv.size(); i += 4u) {
        if (i >= STR_BITS_FRONT_BACK && i < (bv.size() / BV_WORD_BIT_SIZE) * BV_WORD_BIT_SIZE - STR_BITS_FRONT_BACK) {
            if (!wrote_dots) {
                ss << STR_PLACEHOLDER;
                wrote_dots = true;
            }
            continue;
        }

        uint32_t rank = f.rank1(i);
        ss << std::setw(4) << rank;

        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 4u && i < bv.size() - 4u)
            ss << " ";
    }
    return ss.str();
}

std::vector<bool> createRandomBoolVector(uint32_t size = 4000) {
    std::vector<bool> boolVector(size);
    for (uint32_t i = 0; i < size; i++)
        boolVector[i] = dist(mt) >= 0.5;
    return boolVector;
}

BitVector createRandomBitVector(uint32_t size = 4000) {
    BitVector bitVector = BitVector(size);
    for (uint32_t i = 0; i < size; i++)
        bitVector.set(i, dist(mt) >= 0.5);
    return bitVector;
}

BitVector createBitVectorFromBoolVector(std::vector<bool> &boolVector) {
    BitVector bitVector = BitVector(boolVector.size());
    for (uint32_t i = 0; i < boolVector.size(); i++)
        bitVector.set(i, boolVector[i]);
    return bitVector;
}

std::vector<uint32_t> createRandomNibbleVector(uint32_t size = 4000) {
    std::vector<uint32_t> v((size + 7) / 8); // 8 nibbles (half bytes) in uint32
    for (uint32_t i = 0; i < size; i++) {
        write4Bit(v, 0, i, static_cast<uint32_t>(dist(mt) * 16u));
    }
    return v;
}

int test_bv_set_access(uint32_t size = 4000) {
    std::vector<bool> boolVector = createRandomBoolVector(size);
    BitVector bitVector = BitVector(boolVector);

    // test initial creation from bool vector (linear access)
    for (uint32_t i = 0u; i < size; i++) {
        if (boolVector[i] != bitVector.access(i))
            return static_cast<int>(i) + 1;
    }

    // test after switching bits at random positions (random access)
    for (uint32_t i = 0u; i < size / 2u; i++) {
        auto random_pos = static_cast<uint32_t>(dist(mt) * size);

        boolVector[random_pos] = !boolVector[random_pos];
        bitVector.set(random_pos, boolVector[random_pos]);
    }
    for (uint32_t i = 0u; i < size; i++) {
        if (boolVector[i] != bitVector.access(i))
            return static_cast<int>(i) + 1;
    }

    return 0;
}

int test_bv_rank(uint32_t size = 4000) {
    BitVector bitVector = createRandomBitVector(size);
    FlatRank f(bitVector);

    for (uint32_t i = 0; i < size; i += 1u) {
        // compute with FlatRank
        uint32_t rank = f.rank1(i);

        // compute reference
        uint32_t ref_rank = 0u;
        for (int n = 0; n < i; n++)
            ref_rank += bitVector.access(n);

        if (ref_rank != rank)
            return static_cast<int>(i) + 1;
    }

    return 0;
}

int test_wm() {
    auto v4bit = createRandomNibbleVector();
    WaveletMatrix m(v4bit.data(), 0, v4bit.size() * 8);

    for (int i = 0; i < v4bit.size() * 8; i++) {
        // access
        uint32_t ref_access = read4Bit(v4bit, 0, i);
        if (m.access(i) != ref_access) {
            Logger(Error) << m.access(i) << " instead of " << ref_access;
            return i + 1;
        }

        // rank
        for (uint32_t s = 0; s < WM_ALPHABET_SIZE; s++) {
            uint32_t ref_rank = 0u;
            for (int n = 0; n < i; n++) {
                if (read4Bit(v4bit, 0, n) == s)
                    ref_rank++;
            }
            if (ref_rank != m.rank(i, s))
                return -(i + 1);
        }
    }
    return 0;
}

void printWMTest() {
    constexpr uint32_t size = 32 * 32 * 32 + 16 * 16 * 16 + 8 * 8 * 8 + 4 * 4 * 4 + 2 * 2 * 2 + 1;
    auto v4bit = createRandomNibbleVector(size);
    WaveletMatrix m(v4bit.data(), 0, size);

    // print some information about the WM structure
    constexpr int TIMER_RUN_COUNT = 10;
    vvv::MiniTimer t;
    uint32_t x = 0u;
    double e_rank = 0.f;
    for (int runs = 0; runs < TIMER_RUN_COUNT; runs++) {
        t.restart();
        for (int i = 0; i < size; i++)
            x ^= m.rank(i, i % 16u);
        e_rank += t.elapsed();
    }
    e_rank /= TIMER_RUN_COUNT;
    double e_access = 0.f;
    for (int runs = 0; runs < TIMER_RUN_COUNT; runs++) {
        t.restart();
        for (int i = 0; i < size; i++)
            x ^= m.access(i);
        e_access += t.elapsed();
    }
    e_access /= TIMER_RUN_COUNT;
    uint32_t byte_size = m.getByteSize();
    std::cout << "Wavelet Matrix rank() in " << e_rank / static_cast<double>(size) * 1000. * 1000. * 1000. << " ns,"
              << " access in " << e_access / static_cast<double>(size) * 1000. * 1000. * 1000. << " ns,"
              << "space overhead is "
              << (static_cast<double>(byte_size) / (v4bit.size() * 4.) * 100.f) << "% compared to 4 bits per entry"
              << (x & 1u ? " " : "") // dependency to ensure that f.rank1(i) is not optimized away
              << std::endl;
    //    // L2_PER_L1,L1_BITS,L2_BITS,L2_BIT_SIZE,timing,space
    //    std::cout << BV_STORE_L2_PER_L1 << "," << BV_STORE_L1_BITS << "," << BV_STORE_L2_BITS << ","
    //              << BV_L2_BIT_SIZE << "," << e/static_cast<double>(size)*1000.*1000.*1000. << ","
    //              << FlatRank::overhead() << std::endl;
    std::cout << std::endl;
}

void printBVTest() {
    constexpr uint32_t size = 64 * 64 * 64;
    auto bv = createRandomBitVector(size);
    std::cout << "     Bit Vector: " << str(bv) << std::endl;
    std::cout << "                 " << rankStrTicks(bv) << std::endl;
    std::cout << "rank1 reference: " << rankStrReference(bv) << std::endl;
    FlatRank f = FlatRank(bv);
    std::cout << "rank1 flat rank: " << rankStrFlatRank(f, bv) << std::endl;

    // print some information about the FlatRank structure
    uint32_t x = 0u;
    double e = 0.f;
    for (int runs = 0; runs < 10000; runs++) {
        vvv::MiniTimer t;
        for (int i = 0; i < size; i++)
            x ^= f.rank1(i);
        e += t.elapsed();
    }
    e /= 10000.f;
    uint32_t max_bv_size = FlatRank::maximumBitVectorSize();
    std::cout << "FlatRank rank1() in " << e / static_cast<double>(size) * 1000. * 1000. * 1000. << " ns, space overhead is "
              << FlatRank::overhead() * 100.f << "%, maximum bit vector size is "
              << FlatRank::maximumBitVectorSize() << " (64³ brick has 299593 entries)"
              << (x & 1u ? " " : "") // dependency to ensure that f.rank1(i) is not optimized away
              << std::endl;
    //    // L2_PER_L1,L1_BITS,L2_BITS,L2_BIT_SIZE,timing,space
    //    std::cout << BV_STORE_L2_PER_L1 << "," << BV_STORE_L1_BITS << "," << BV_STORE_L2_BITS << ","
    //              << BV_L2_BIT_SIZE << "," << e/static_cast<double>(size)*1000.*1000.*1000. << ","
    //              << FlatRank::overhead() << std::endl;
    std::cout << std::endl;
}

int main(int argc, char *argv[]) {

    // printBVTest();
    // printWMTest();

    if (test_bv_set_access())
        return 1;
    else if (test_bv_rank())
        return 2;
    else if (test_wm())
        return 3;
}
