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
#include "vvv/volren/Volume.hpp"
#include "volcanite/utility/segmentation_volume_synthesis.hpp"

using namespace volcanite;

int main() {

    // create dummy segmentation volume (must be dividable by brick size for random access decoding)
    glm::uvec3 dim = {128, 64, 192};
    const auto volume = createDummySegmentationVolume(dim);

    CompressedSegmentationVolume csgv;
    {
        Logger(INFO) << "Random Access 4 Bit";
        csgv.setCompressionOptions64(32, NIBBLE_ENC, OP_ALL_WITHOUT_STOP, true);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 1;
    }
    csgv.clear();
    {
        Logger(INFO) << "Random Access Wavelet Matrix";
        csgv.setCompressionOptions64(16, WAVELET_MATRIX_ENC, OP_ALL_WITHOUT_STOP, true);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 2;
    }
    csgv.clear();
    {
        Logger(INFO) << "Random Access Huffman Wavelet Matrix";
        csgv.setCompressionOptions64(32, HUFFMAN_WM_ENC, OP_ALL_WITHOUT_STOP, true);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 3;
    }
    csgv.clear();
    {
        Logger(INFO) << "Random Access Huffman Wavelet Matrix with Stop Bits";
        csgv.setCompressionOptions64(64, HUFFMAN_WM_ENC, OP_ALL, true);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 4;
    }

    return 0;
}