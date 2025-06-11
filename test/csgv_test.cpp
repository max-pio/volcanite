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
        csgv.setCompressionOptions({.brick_size=16, .encoding_mode=NIBBLE_ENC, .op_mask=OP_ALL, .random_access=false});
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
        csgv.setCompressionOptions({.brick_size=32, .encoding_mode=NIBBLE_ENC, .op_mask=OP_ALL, .random_access=false});
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, false, false);
        csgv.setCompressionOptions({.brick_size=32, .encoding_mode=SINGLE_TABLE_RANS_ENC, .op_mask=OP_ALL, .random_access=false, .code_frequencies=freq, .detail_code_frequencies=(freq + 16)});
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
        csgv.setCompressionOptions({.brick_size=64, .encoding_mode=NIBBLE_ENC, .op_mask=OP_ALL, .random_access=false});
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, true, false);
        csgv.setCompressionOptions({.brick_size=64, .encoding_mode=DOUBLE_TABLE_RANS_ENC, .op_mask=OP_ALL, .random_access=false, .code_frequencies=freq, .detail_code_frequencies=(freq + 16)});
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
            csgv.setCompressionOptions({.brick_size=32, .encoding_mode=WAVELET_MATRIX_ENC, .op_mask=(OP_ALL_WITHOUT_STOP & OP_ALL_WITHOUT_DELTA), .random_access=true});
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
            csgv.setCompressionOptions({.brick_size=16, .encoding_mode=HUFFMAN_WM_ENC, .op_mask=OP_ALL_WITHOUT_DELTA, .random_access=true});
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
            csgv.setCompressionOptions({.brick_size=64, .encoding_mode=HUFFMAN_WM_ENC, .op_mask=OP_ALL_WITHOUT_DELTA, .random_access=true});
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
