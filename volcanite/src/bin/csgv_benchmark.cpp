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

#include "vvv/core/HeadlessRendering.hpp"
#include "vvv/headless_entrypoint.hpp"
#include "vvv/util/Logger.hpp"
#include <string>

#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/eval/CSGVBenchmarkPass.hpp"
#include "volcanite/util/args_and_csgv_provider.hpp"

using namespace volcanite;

int volcanite_main(int argc, char *argv[]) {
    // parse command line arguments
    std::shared_ptr<CompressedSegmentationVolume> compressedSegmentationVolume;
    std::shared_ptr<CSGVDatabase> csgvDatabase;
    VolcaniteArgs args;
    auto ret = volcanite_provide_args_and_csgv(args, compressedSegmentationVolume, csgvDatabase, argc, argv);
    if (ret != RET_SUCCESS) {
        return ret;
    }

    if (args.performDecompression()) {
        // TODO: add decompression
        Logger(Error) << "decompression not yet supported";
        return RET_NOT_SUPPORTED;
    }

    if (args.export_stats) {
        Logger(Info, true) << "export brick statistics...";
        std::string stats_path = stripFileExtension(args.input_file) + "_brickstats.csv";
        csv_export(compressedSegmentationVolume->gatherBrickStatistics(), stats_path);
        Logger(Info) << "export brick statistics to " << stats_path + " done";
    }

    // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
    if (args.stream_lod && !compressedSegmentationVolume->isUsingSeparateDetail()) {
        Logger(Debug) << "separating detail level encoding.";
        compressedSegmentationVolume->separateDetail();
        Logger(Debug) << compressedSegmentationVolume->getEncodingInfoString();
    }

    Logger(Info) << "--------------------------------------------------- ";
    Logger(Info) << "Starting CSGV GPU decompression benchmark";

    DefaultGpuContext ctx;
    ctx.enableDeviceExtension("VK_EXT_memory_budget");
    ctx.physicalDeviceFeatures().setShaderInt64(true);
    ctx.physicalDeviceFeaturesV12().setBufferDeviceAddress(true);
    ctx.physicalDeviceFeaturesV12().setHostQueryReset(true);
    ctx.createGpuContext();
    CSGVBenchmarkPass benchmark(&(*compressedSegmentationVolume), &ctx, args.cache_size_MB,
                                args.cache_palettized, args.decode_from_shared_memory);

    std::shared_ptr<Awaitable> awaitable = benchmark.execute();
    ctx.sync->hostWaitOnDevice({awaitable});

    double execution_time = 0.f;
    do {
        execution_time = benchmark.getExecutionTimeMS();
    } while (execution_time == 0.f);

    size_t volume_size_byte = static_cast<size_t>(compressedSegmentationVolume->getVolumeDim().x) * compressedSegmentationVolume->getVolumeDim().y * compressedSegmentationVolume->getVolumeDim().z * sizeof(uint32_t);
    Logger(Info) << "GPU decompression time: " << execution_time << " ms ("
                 << (static_cast<double>(volume_size_byte) / 1000. / 1000. / 1000.) / (execution_time / 1000.)
                 << " GB/s).";

    benchmark.freeResources();
    return RET_SUCCESS;
}

ENTRYPOINT(volcanite_main)
