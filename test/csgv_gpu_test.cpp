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

#include "volcanite/eval/CSGVBenchmarkPass.hpp"
#include "vvv/core/DefaultGpuContext.hpp"

using namespace volcanite;
using namespace vvv;

int main() {
    constexpr uint32_t cache_size_mb = 16;

    // initialize data paths to shaders
    Paths::initPaths(DATA_DIRS);

    // create GPU context
    Logger(Info, true) << "Create GPU context..";
    DefaultGpuContext ctx;
    CSGVBenchmarkPass::configureExtensionsAndLayersAndFeatures(&ctx);
    ctx.createGpuContext();
    Logger(Info) << "Create GPU context (ok)";

    // create dummy segmentation volume
    glm::uvec3 dim = {100, 80, 95};
    const auto volume = createDummySegmentationVolume({.dim = dim});

    CompressedSegmentationVolume csgv;
    {
        Logger(Info) << "Nibble";
        csgv.setCompressionOptions64(32, NIBBLE_ENC, OP_ALL, false);
        csgv.compress(volume->dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();

        Logger(Info) << "Range ANS with Palettized Cache";
        size_t freq[32];
        csgv.setCompressionOptions64(64, NIBBLE_ENC, OP_ALL, false);
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, false, false);
        csgv.setCompressionOptions64(64, SINGLE_TABLE_RANS_ENC, OP_ALL, false, freq, freq + 16);
        csgv.compress(volume->dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, true, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();

        Logger(Info) << "Double Table Range ANS";
        csgv.setCompressionOptions64(16, NIBBLE_ENC, OP_ALL, false);
        csgv.compressForFrequencyTable(volume->dataConst(), dim, freq, 2, true, false);
        csgv.setCompressionOptions64(16, DOUBLE_TABLE_RANS_ENC, OP_ALL, false, freq, freq + 16);
        csgv.compress(volume->dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();
    }

    // Random Access Decoding
    {
        Logger(Info) << "Random Access Nibble";
        csgv.setCompressionOptions64(32, NIBBLE_ENC, OP_ALL_WITHOUT_STOP & OP_ALL_WITHOUT_DELTA, true);
        csgv.compress(volume->dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        Logger(Info) << "Random Access Nibble (Shared Memory)";
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, true);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();

        Logger(Info) << "Random Access Huffman Shaped Wavelet Matrix";
        csgv.setCompressionOptions64(16, HUFFMAN_WM_ENC, OP_ALL_WITHOUT_DELTA, true);
        csgv.compress(volume->dataConst(), dim, false);
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, false);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        Logger(Info) << "Random Access Huffman Shaped Wavelet Matrix (Shared Memory)";
        {
            CSGVBenchmarkPass benchmark(&csgv, &ctx, cache_size_mb, false, true);
            std::shared_ptr<Awaitable> awaitable = benchmark.execute();
            ctx.sync->hostWaitOnDevice({awaitable});
            benchmark.freeResources();
        }
        csgv.clear();
    }

    return 0;
}
