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

#define HEADLESS

#include "vvv/volren/Volume.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/utility/segmentation_volume_synthesis.hpp"

#include "vvv/core/DefaultGpuContext.hpp"
#include "volcanite/benchmark/CSGVBenchmarkPass.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/core/HeadlessRendering.hpp"
#include "stb/stb_image.hpp"

using namespace volcanite;
using namespace vvv;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;
constexpr int RET_EXPORT_ERROR = 5;


int export_texture(Texture* tex, const std::string& export_file_path) {
    try {
        Logger(INFO) << "Exporting render output to " << export_file_path;
        tex->writeFile(export_file_path);
    }
    catch(const std::runtime_error& e) {
        Logger(ERROR) << "Render export error: " << e.what();
        return RET_EXPORT_ERROR;
    }
    return 0;
}

int renderImageToFile(const std::shared_ptr<CompressedSegmentationVolume>& csgv,
                      const std::shared_ptr<CSGVDatabase>& csgvDatabase,
                      const VolcaniteArgs& args) {
    // if the attribute database is a dummy, update the min/max attribute values for the volume labels
    if(csgvDatabase->isDummy())
        csgvDatabase->updateDummyMinMax(*csgv);

    const auto renderer = std::make_shared<volcanite::CompressedSegmentationVolumeRenderer>(!args.show_development_gui);
    renderer->setDecodingParameters(args.cache_size_MB, args.cache_palettized);
    renderer->setCompressedSegmentationVolume(csgv, csgvDatabase);
    // not setting render config: use default values
    renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});

    // obtain a headless rendering engine
    auto renderEngine = HeadlessRendering::create("Volcanite Render Test " + args.screenshot_output_file,
                                                  renderer, std::make_shared<DebugUtilsExt>());
    renderEngine->acquireResources();
    // let the rendering converge for some frames (if specified in the rendering config, we use that number)
    int accumulation_frames = renderer->getTargetAccumulationFrames();
    auto texture = renderEngine->renderFrames(accumulation_frames > 0 ? accumulation_frames : 300);
    if(texture == nullptr) {
        Logger(ERROR) << "internal rendering error";
        return RET_RENDER_ERROR;
    }
    if(export_texture(texture.get(), args.screenshot_output_file) != RET_SUCCESS) {
        Logger(ERROR) << "image export error";
        return RET_EXPORT_ERROR;
    }
    texture.reset();
    texture = nullptr;
    renderEngine->releaseResources();

    return RET_SUCCESS;
}

static const std::string OUT_DIR = "./render_test/";
static const std::vector<VolcaniteArgs> RENDERING_TEST_CONFIGS = {
        {.screenshot_output_file=OUT_DIR + "nibble_32.png", .brick_size=32, .encoding_mode=NIBBLE_ENC},
        {.screenshot_output_file=OUT_DIR + "rANSd_64_cache-palette.png", .cache_palettized=true, .brick_size=64, .encoding_mode=SINGLE_TABLE_RANS_ENC},
        {.screenshot_output_file=OUT_DIR + "rANS_16_stream-lod.png", .stream_lod=true, .brick_size=16, .encoding_mode=DOUBLE_TABLE_RANS_ENC}
    };

/// Compares two images for equality as a sum of pixel and channel-wise differences as
/// max(|img_a[x,y,c] - img_b[x,y,c]| - epsilon, 0) for all x,y,c.
/// \param path1 first image
/// \param path2 second image
/// \param epsilon difference per pixel that is not counted as an error in [0,255]
/// \return 0 for equality, negative values for image loading errors, otherwise the sum of pixel-wise differences.
long long compareImages(const std::string& path1, const std::string& path2, unsigned char epsilon) {
    int w1, h1, c1, w2, h2, c2;
    unsigned char* image1 = stbi_load(path1.c_str(), &w1, &h1, &c1, 0);
    unsigned char* image2 = stbi_load(path2.c_str(), &w2, &h2, &c2, 0);
    if (image1 == nullptr || image2 == nullptr)
        return -2ll;
    if (w1 != w2 || h1 != h2 || c1 != c2) {
        return -1ll;
    }

    size_t element_count = w1 * h1 * c1;
    long long differences = 0ll;
    #pragma omp parallel for default(none) shared(element_count, image1, image2, epsilon) reduction(+ : differences)
    for (size_t i = 0; i < element_count; i++) {
        int diff = std::abs(static_cast<int>(image1[i]) - static_cast<int>(image2[i])) - static_cast<int>(epsilon);
        if (diff > 0)
            differences += diff;
    }

    stbi_image_free(image1);
    stbi_image_free(image2);
    return differences;
}

/// Renders one image with the same rendering config for different CSGV encoding and decoding modes using the Headless
/// renderer. All output images are compared for differences. The encoding and decoding properties should not change
/// anything in the converged output frame significantly.
int main() {
    // initialize data paths to shaders
    vvv::Paths::initPaths(DATA_DIRS);

    // create headless rendering engine with GPU context

    // create dummy segmentation volume
    glm::uvec3 dim = {100, 80, 95};
    const auto volume = createDummySegmentationVolume(dim);

    // create compressed segmentation volume
    std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<CompressedSegmentationVolume>();
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase = std::make_shared<volcanite::CSGVDatabase>();
    csgvDatabase->createDummy();
    size_t freq[32];

    // for all test configurations: export one render image each
    for (const auto& args : RENDERING_TEST_CONFIGS) {
        if(!args.screenshot_output_file.ends_with(".png")) {
            Logger(ERROR) << "must provide export file path for render test run as '*.png'";
            return RET_INVALID_ARG;
        }
        Logger(INFO) << "Rendering output " << args.screenshot_output_file;

        // compress the volume
        csgv->clear();
        if (args.encoding_mode == SINGLE_TABLE_RANS_ENC || args.encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            // obtain frequency table(s)
            csgv->setCompressionOptions64(args.brick_size, NIBBLE_ENC);
            csgv->compressForFrequencyTable(volume.dataConst(), dim, freq, 2, args.encoding_mode == DOUBLE_TABLE_RANS_ENC, false);
        }
        csgv->setCompressionOptions64(args.brick_size, args.encoding_mode, freq, freq + 16);
        csgv->compress(volume.dataConst(), dim, false);
        // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
        if(args.stream_lod && !csgv->isUsingSeparateDetail()) {
            csgv->separateDetail();
        }

        // render one output image
        int ret = renderImageToFile(csgv, csgvDatabase, args);
        if (ret != RET_SUCCESS)
            return ret;
    }

    // check output image files for pair-wise equality
    for (int img_a = 0; img_a < RENDERING_TEST_CONFIGS.size(); img_a++) {
        for (int img_b = img_a + 1; img_b < RENDERING_TEST_CONFIGS.size(); img_b++) {
            long long differences = compareImages(RENDERING_TEST_CONFIGS[img_a].screenshot_output_file,
                                                  RENDERING_TEST_CONFIGS[img_b].screenshot_output_file, 0);
            if (differences < 0ll) {
                Logger(ERROR) << "Image loading error for "
                              << RENDERING_TEST_CONFIGS[img_a].screenshot_output_file << " and "
                              << RENDERING_TEST_CONFIGS[img_b].screenshot_output_file;
            } else if (differences > 0ull) {
                Logger(ERROR) << "Rendering differences with absolute pixel error sum of " << differences
                              << " for images " << RENDERING_TEST_CONFIGS[img_a].screenshot_output_file << " and "
                              << RENDERING_TEST_CONFIGS[img_b].screenshot_output_file;
                return RET_RENDER_ERROR;
            }
        }
    }

    return RET_SUCCESS;
}