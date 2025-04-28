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

#include "vvv/volren/Volume.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/util/segmentation_volume_synthesis.hpp"

using namespace volcanite;

#include "volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp"
#include "volcanite/compression/pack_nibble.hpp"
#include "volcanite/compression/wavelet_tree/WaveletMatrix.hpp"

std::vector<uint32_t> random4BitOperationStream(uint32_t length) {
    std::vector<uint32_t> v((length + 7u) / 8u, 0u);
    for (auto i = 0; i < length; i++) {
        uint32_t op = std::rand() % 6u;
        write4Bit(v, 0, i, op);
    }
    return v;
}

std::vector<uint32_t> cycle4BitOperationStream(uint32_t length) {
    std::vector<uint32_t> v((length + 7u) / 8u, 0u);
    for (auto i = 0; i < length; i++) {
        uint32_t op = i % 6u;
        write4Bit(v, 0, i, op);
    }
    return v;
}

uint32_t rank_scan(std::vector<uint32_t>& v, uint32_t pos, uint32_t op) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < pos; i++) {
        if (read4Bit(v, 0, i) == op)
            count++;
    }
    return count;
}

int main() {

    // short text, evenly dividable by BV_WORD_SIZE
    uint32_t text_size = 128;
    auto ops = cycle4BitOperationStream(text_size);

    {
        // test all Wavelet Matrix implementations in the same loop
        auto wm = WaveletMatrix(ops.data(), 0, text_size);
        auto wmh = HuffmanWaveletMatrix(ops.data(), 0, text_size);

        uint32_t expected, got;
        for (auto i = 0; i < text_size; i++) {
            // access
            expected = read4Bit(ops, 0, i);
            got = wmh.access(i);
            if (expected != got)
                return 10;
            got = wmh.access(i);
            if (expected != got)
                return 20;


            // rank
            for (int op = 0; op < 6; op++) {
                expected = rank_scan(ops, i, op);
                got = wmh.rank(i, op);
                if (expected != got)
                    return 11;
                got = wmh.rank(i, op);
                if (expected != got)
                    return 21;
            }
        }
    }

    // longer text with random operations
    text_size = 8661;
    ops = random4BitOperationStream(text_size);
    {
        // test all Wavelet Matrix implementations in the same loop
        auto wm = WaveletMatrix(ops.data(), 0, text_size);
        auto wmh = HuffmanWaveletMatrix(ops.data(), 0, text_size);

        uint32_t expected, got;
        for (auto i = 0; i < text_size; i++) {
            // access
            expected = read4Bit(ops, 0, i);
            got = wmh.access(i);
            if (expected != got)
                return 110;
            got = wmh.access(i);
            if (expected != got)
                return 120;


            // rank
            for (int op = 0; op < 6; op++) {
                expected = rank_scan(ops, i, op);
                got = wmh.rank(i, op);
                if (expected != got)
                    return 111;
                got = wmh.rank(i, op);
                if (expected != got)
                    return 121;
            }
        }
    }

    return 0;
}