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

#include "vvv/headless_entrypoint.hpp"
#include "volcanite/compression/BitVector.hpp"

#include <iostream>
#include <string>
#include <random>

using namespace volcanite;

std::random_device rd;
std::mt19937 mt(rd());
std::uniform_real_distribution<double> dist(0., 1.);

std::string booleansStr(const std::vector<bool>& bv) {
    std::stringstream ss;
    for (uint32_t i = 0; i < bv.size(); i++) {
        ss << (bv[i] ? '1' : '0');
        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 1u && i < bv.size() - 1u)
            ss << " ";
    }
    return ss.str();
}

int test_set_access(uint32_t size = 100) {
    std::cout << "test: set access linear" << std::endl;
    BitVector bitVector = BitVector(size);
    std::vector<bool> boolVector(size);

    for (uint32_t i = 0; i < size; i++) {
        boolVector[i] = dist(mt) >= 0.5;
        bitVector.set(i, boolVector[i]);
    }
    std::cout << booleansStr(boolVector) << std::endl;
    std::cout << bitVector.str() << std::endl;

    std::cout << std::endl;

    std::cout << "test: set access random" << std::endl;
    for (uint32_t i = 0u; i < size / 2u; i++) {
        // switch bits at random positions
        uint32_t random_pos = static_cast<uint32_t>(dist(mt) * size);
        boolVector[random_pos] = !boolVector[random_pos];

        bitVector.set(random_pos, boolVector[random_pos]);
    }
    std::cout << booleansStr(boolVector) << std::endl;
    std::cout << bitVector.str() << std::endl;

    std::cout << std::endl;

    return 0;
}

int test_rank(uint32_t size = 100) {
    std::cout << "test: push_back rank1 level" << std::endl;

    BitVector bitVector;

    std::stringstream ss;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t bit_value = dist(mt) >= 0.5;
        ss << (bit_value ? '1' : '0');
        if (i % BV_WORD_BIT_SIZE == BV_WORD_BIT_SIZE - 1u && i < size - 1u)
            ss << " ";
        bitVector.push_back(bit_value);
    }
    bitVector.shrink_to_fit();
    std::cout << bitVector.str() << std::endl;

    for (uint32_t i = 0; i < size; i++) {
        uint32_t rank = 0u;
        for(int n=0; n < i; n++) {
            rank += bitVector.access(n);
        }
        std::cout << rank << " ";
    }
    std::cout << std::endl;

    FlatRank f(bitVector);
    for (uint32_t i = 0; i < size; i++) {
        uint32_t rank = f.rank1(i);
        std::cout << rank << " ";
    }

    std::cout << std::endl;
    std::cout << std::endl;

    return 0;
}

int bitvector_test(int argc, char *argv[]) {

    //test_set_access();
    test_rank();

    return 0;
}

ENTRYPOINT(bitvector_test)
