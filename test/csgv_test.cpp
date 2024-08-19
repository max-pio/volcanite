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
#include "volcanite/utility/segmentation_volume_synthesis.hpp"

using namespace volcanite;

int main() {

    // create dummy segmentation volume
    glm::uvec3 dim = {100, 80, 95};
    const auto volume = createDummySegmentationVolume(dim);

    CompressedSegmentationVolume csgv;
    // Plain 4 bit per operation encoding
    {
        Logger(INFO) << "TEST: NO RANS";
        csgv.setCompressionOptions64(16, RANSMode::NO_RANS, false);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 1;

        // export / re-import
        std::remove("./_tmp_test.csgv");
        csgv.exportToFile("./_tmp_test.csgv");
        if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume.dataConst(), dim, false))
            return 101;
    }
    csgv.clear();
    // Single table rANS
    {
        Logger(INFO) << "TEST: RANS";
        size_t freq[32];
        csgv.setCompressionOptions64(32, NO_RANS, false);
        csgv.compressForFrequencyTable(volume.dataConst(), dim, freq, 2, false, false);
        csgv.setCompressionOptions64(32, RANSMode::SINGLE_TABLE_RANS, false, freq, freq + 16);
        if (!csgv.test(volume.dataConst(), dim, true))
            return 2;

        // export / re-import
        std::remove("./_tmp_test.csgv");
        csgv.exportToFile("./_tmp_test.csgv");
        if (!csgv.importFromFile("./_tmp_test.csgv") || !csgv.test(volume.dataConst(), dim, false))
            return 102;
    }
    csgv.clear();
    // Double table rANS with detail separation
    {
        Logger(INFO) << "TEST: DOUBLE TABLE RANS, DETAIL SEPARATION";
        size_t freq[32];
        csgv.setCompressionOptions64(32, NO_RANS, false);
        csgv.compressForFrequencyTable(volume.dataConst(), dim, freq, 2, true, false);
        csgv.setCompressionOptions64(64, RANSMode::DOUBLE_TABLE_RANS, false, freq, freq + 16);
        csgv.compress(volume.dataConst(), dim, false);
//        csgv.separateDetail();
        if (!csgv.test(volume.dataConst(), dim, false))
            return 3;
    }

    std::remove("./_tmp_test.csgv");
    return 0;
}