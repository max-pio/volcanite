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

#include "vvv/core/DefaultGpuContext.hpp"
#include "volcanite/benchmark/CSGVBenchmarkPass.hpp"

using namespace volcanite;
using namespace vvv;

int main() {
    const uint32_t cache_size_mb = 16;

    // initialize data paths to shaders
    vvv::Paths::initPaths(DATA_DIRS);

    // create GPU context
    Logger(INFO, true) << "Create GPU context..";
    DefaultGpuContext ctx;
    CSGVBenchmarkPass::configureExtensionsAndLayersAndFeatures(&ctx);
    ctx.createGpuContext();
    Logger(INFO) << "Create GPU context (ok)";

    CompressedSegmentationVolume csgv;
    {
        // create dummy segmentation volume
        glm::uvec3 dim = {100, 80, 95};
        const auto volume = createDummySegmentationVolume(dim);

        Logger(INFO) << "Nibble";
        csgv.setCompressionOptions64(32, NIBBLE_ENC);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();

        Logger(INFO) << "Range ANS with Palettized Cache";
        size_t freq[32];
        csgv.setCompressionOptions64(64, NIBBLE_ENC);
        csgv.compressForFrequencyTable(volume.dataConst(), dim, freq, 2, false, false);
        csgv.setCompressionOptions64(64, SINGLE_TABLE_RANS_ENC, freq, freq + 16);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();

        Logger(INFO) << "Double Table Range ANS";
        csgv.setCompressionOptions64(16, NIBBLE_ENC);
        csgv.compressForFrequencyTable(volume.dataConst(), dim, freq, 2, true, false);
        csgv.setCompressionOptions64(16, DOUBLE_TABLE_RANS_ENC, freq, freq + 16);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
    }

    return 0;
}