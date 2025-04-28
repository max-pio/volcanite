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

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/util/segmentation_volume_synthesis.hpp"
#include "vvv/volren/Volume.hpp"

using namespace volcanite;

int main() {

    // create dummy segmentation volume
    glm::uvec3 dim = {100, 80, 95};
    const auto volume = createDummySegmentationVolume({.dim = dim});

    CompressedSegmentationVolume csgv;
    // Plain 4 bit per operation encoding
    {
        Logger(Info) << "Nibble";
        csgv.setCompressionOptions64(16, NIBBLE_ENC, OP_ALL, false);
        if (!csgv.test(volume->dataConst(), dim, true))
            return 1;

        // export / re-import
        std::remove("./_tmp_test.csgv");
        csgv.exportToFile("./_tmp_test.csgv");
        if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume->dataConst(), dim, false))
            return 101;
    }
    csgv.clear();
    // Single table rANS
    {
        Logger(Info) << "Range ANS";
        size_t freq[32];
        csgv.setCompressionOptions64(32, NIBBLE_ENC, OP_ALL, false);
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, false, false);
        csgv.setCompressionOptions64(32, SINGLE_TABLE_RANS_ENC, OP_ALL, false, freq, freq + 16);
        if (!csgv.test(volume->dataConst(), dim, true))
            return 2;

        // export / re-import
        std::remove("./_tmp_test.csgv");
        csgv.exportToFile("./_tmp_test.csgv");
        if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume->dataConst(), dim, false))
            return 102;
    }
    csgv.clear();
    // Double table rANS with detail separation
    {
        Logger(Info) << "Double Table Range ANS with Detail Separation";
        size_t freq[32];
        csgv.setCompressionOptions64(64, NIBBLE_ENC, OP_ALL, false);
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, true, false);
        csgv.setCompressionOptions64(64, DOUBLE_TABLE_RANS_ENC, OP_ALL, false, freq, freq + 16);
        csgv.compress(volume->dataConst(), dim, false);
        csgv.separateDetail();
        if (!csgv.test(volume->dataConst(), dim, false))
            return 3;
    }

    // Random Access Encoding
    {
        // Wavelet Matrix
        {
            Logger(Info) << "Wavelet Matrix";
            csgv.setCompressionOptions64(32, WAVELET_MATRIX_ENC, OP_ALL_WITHOUT_STOP & OP_ALL_WITHOUT_DELTA, true);
            if (!csgv.test(volume->dataConst(), dim, true))
                return 4;

            // export / re-import
            std::remove("./_tmp_test.csgv");
            csgv.exportToFile("./_tmp_test.csgv");
            if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume->dataConst(), dim, false))
                return 104;
        }
        // Huffman Wavelet Matrix
        {
            Logger(Info) << "Wavelet Matrix";
            csgv.setCompressionOptions64(16, HUFFMAN_WM_ENC, OP_ALL_WITHOUT_DELTA, true);
            if (!csgv.test(volume->dataConst(), dim, true))
                return 5;

            // export / re-import
            std::remove("./_tmp_test.csgv");
            csgv.exportToFile("./_tmp_test.csgv");
            if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume->dataConst(), dim, false))
                return 105;
        }
        // Huffman Wavelet Matrix with Stop Bits
        {
            Logger(Info) << "Wavelet Matrix";
            csgv.setCompressionOptions64(64, HUFFMAN_WM_ENC, OP_ALL_WITHOUT_DELTA, true);
            if (!csgv.test(volume->dataConst(), dim, true))
                return 6;

            // export / re-import
            std::remove("./_tmp_test.csgv");
            csgv.exportToFile("./_tmp_test.csgv");
            if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume->dataConst(), dim, false))
                return 106;
        }
    }

    std::remove("./_tmp_test.csgv");
    return 0;
}
