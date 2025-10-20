//  Copyright (C) 2024, Max Piochowiak and Fabian Schiekel Karlsruhe Institute of Technology
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

#define HEADLESS

#include "shaderc/third_party/spirv-tools/include/spirv-tools/libspirv.h"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/util/segmentation_volume_synthesis.hpp"
#include "vvv/volren/Volume.hpp"

#include "glm/ext.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/eval/CSGVBenchmarkPass.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include <string>

using namespace volcanite;
using namespace vvv;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;
constexpr int RET_EXPORT_ERROR = 5;

struct DECOMPRESSION_TEST_CONFIGS {
    glm::uvec3 volumeDim;
    glm::uvec3 chunkSize;
    glm::uvec3 maxFileIndex; // max file id of chunked test volume
};

int main() {
    // initialize data paths to shaders
    vvv::Paths::initPaths(DATA_DIRS);
    std::vector<DECOMPRESSION_TEST_CONFIGS> testConfig = {
        {{128, 256, 256}, {64, 128, 256}, {1, 1, 0}},
        {{156, 105, 54}, {64, 64, 64}, {2, 1, 0}},
    };

    for (auto &config : testConfig) {
        // create dummy segmentation volume
        const auto volume = createDummySegmentationVolume({.dim = config.volumeDim, .seed = 0xABCDE12345});

        // create compressed segmentation volume
        std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<CompressedSegmentationVolume>();
        std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase = std::make_shared<volcanite::CSGVDatabase>();
        csgvDatabase->createDummy();
        size_t freq[32];

        csgv->setCompressionOptions({.brick_size = 32, .encoding_mode = NIBBLE_ENC, .op_mask = OP_ALL, .random_access = false});
        // csgv->compressForFrequencyTable(volume->dataConst(), dim, freq, 2, args.encoding_mode == DOUBLE_TABLE_RANS_ENC, false);
        // if (!csgv->test(volume->dataConst(), config.volumeDim, true))
        //     return 1;

        // compress the volume
        csgv->compress(volume->dataConst(), config.volumeDim, false);

        if (!csgv->verifyCompression())
            return RET_COMPR_ERROR;

        // decompress the volume
        const auto decompressed_volume_export_path_base = std::filesystem::temp_directory_path() / "volcanite/render_test";
        if (!std::filesystem::exists(decompressed_volume_export_path_base))
            std::filesystem::create_directories(decompressed_volume_export_path_base);
        else {
            std::filesystem::remove_all(decompressed_volume_export_path_base);
            std::filesystem::create_directories(decompressed_volume_export_path_base);
        }

        const auto decompressed_volume_export_path = decompressed_volume_export_path_base / "decompressed_test_volume.hdf5";
        const auto decompressed_volume_export_format_path = decompressed_volume_export_path_base / "decompressed_test_volume_x{}y{}z{}.hdf5";
        const auto decompressed_volume_export_csgv_path = decompressed_volume_export_path_base / "decompressed_test_volume.csgv";
        CompSegVolHandler::decompressCompressedSegmentationVolume(csgv, decompressed_volume_export_path.string(), config.chunkSize);

        // recompress the previous decompressed volume
        std::shared_ptr<CompressedSegmentationVolume> csgv_recompressed = nullptr;
        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = 32,
                                                        .encoding_mode = NIBBLE_ENC,
                                                        .op_mask = OP_ALL,
                                                        .random_access = false,
                                                        .chunked_input_data = true,
                                                        .max_file_index = config.maxFileIndex,
                                                        .verbose = true};
        csgv_recompressed = CompSegVolHandler().createCompressedSegmentationVolume(decompressed_volume_export_format_path.string(), decompressed_volume_export_csgv_path.string(), cfg);

        auto csgv_data = csgv->decompress();
        auto csgv_recompressed_data = csgv_recompressed->decompress();

        for (uint32_t z = 0; z < config.volumeDim.z; z++) {
            for (uint32_t y = 0; y < config.volumeDim.y; y++) {
                for (uint32_t x = 0; x < config.volumeDim.x; x++) {
                    if (csgv_data->data()[voxel_pos2idx({x, y, z}, config.volumeDim)] != csgv_recompressed_data->data()[voxel_pos2idx({x, y, z}, config.volumeDim)]) {
                        Logger(Error) << "Decompression test failed. Decompressed volume is different to original volume.";
                        return RET_COMPR_ERROR;
                    }
                }
            }
        }
        Logger(Info) << "Decompression test with volume dim " << glm::to_string(config.volumeDim) << " and chunk size " << glm::to_string(config.chunkSize) << " was successful";
        Logger(Info) << "-------------------------------------------------------------";

        // cleanup previously compressed/decompressed data
        std::filesystem::remove_all(decompressed_volume_export_path_base);
    }

    return RET_SUCCESS;
}
