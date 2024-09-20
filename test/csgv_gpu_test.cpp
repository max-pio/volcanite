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

    // create GPU context
    Logger(INFO, true) << "Create GPU context..";
    DefaultGpuContext ctx;
    ctx.enableDeviceExtension("VK_EXT_memory_budget");
    ctx.physicalDeviceFeaturesV12().setBufferDeviceAddress(true);
    ctx.physicalDeviceFeaturesV12().setHostQueryReset(true);
    ctx.createGpuContext();
    Logger(INFO) << "Create GPU context (ok)";

    CompressedSegmentationVolume csgv;
    // Serial Decoding
    {
        // create dummy segmentation volume
        glm::uvec3 dim = {100, 80, 95};
        const auto volume = createDummySegmentationVolume(dim);

        Logger(INFO) << "Nibble";
        csgv.setCompressionOptions64(32, NIBBLE_ENC, false);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }

        Logger(INFO) << "Range ANS with Palettized Cache";
        csgv.setCompressionOptions64(64, SINGLE_TABLE_RANS_ENC, false);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, true);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }

        Logger(INFO) << "Double Table Range ANS with Detail Separation";
        csgv.setCompressionOptions64(16, DOUBLE_TABLE_RANS_ENC, false);
        csgv.separateDetail();
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
    }

    // Random Access Decoding
    {
        // create dummy segmentation volume
        glm::uvec3 dim = {100, 80, 95};
        const auto volume = createDummySegmentationVolume(dim);

        Logger(INFO) << "Random Access Nibble";
        csgv.setCompressionOptions64(32, NIBBLE_ENC, false);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }

        Logger(INFO) << "Random Access Wavelet Matrix";
        csgv.setCompressionOptions64(64, SINGLE_TABLE_RANS_ENC, false);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, true);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }

        Logger(INFO) << "Random Access Huffman Shaped Wavelet Matrix";
        csgv.setCompressionOptions64(64, SINGLE_TABLE_RANS_ENC, false);
        csgv.compress(volume.dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, false, cache_size_mb, true);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
    }

    return 0;
}