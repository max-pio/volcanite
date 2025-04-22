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
#include "volcanite/util/segmentation_volume_synthesis.hpp"

#include "volcanite/eval/CSGVBenchmarkPass.hpp"
#include "volcanite/VolcaniteArgs.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/core/HeadlessRendering.hpp"
#include "stb/stb_image.hpp"
#include <fmt/core.h>
#include <string>

using namespace volcanite;
using namespace vvv;

constexpr int RET_SUCCESS = 0;
constexpr int RET_INVALID_ARG = 1;
constexpr int RET_COMPR_ERROR = 3;
constexpr int RET_RENDER_ERROR = 4;
constexpr int RET_EXPORT_ERROR = 5;

int export_texture(Texture* tex, const std::string& export_file_path) {
    try {
        Logger(Info) << "Exporting render output to " << export_file_path;
        tex->writeFile(export_file_path);
    }
    catch(const std::runtime_error& e) {
        Logger(Error) << "Render export error: " << e.what();
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
    renderer->setDecodingParameters({.cache_size_MB=args.cache_size_MB,
                                     .palettized_cache=args.cache_palettized,
                                     .decode_from_shared_memory=false,
                                     .cache_mode=args.cache_mode,
                                     .empty_space_resolution=args.empty_space_resolution,
                                     .shader_defines=args.shader_defines});
    renderer->setCompressedSegmentationVolume(csgv, csgvDatabase);
    // not setting render config: use default values
    renderer->setRenderResolution({args.render_resolution[0], args.render_resolution[1]});

    // obtain a headless rendering engine
    auto renderEngine = HeadlessRendering::create("Volcanite Render Test " + args.screenshot_output_file,
                                                  renderer, std::make_shared<DebugUtilsExt>());
    renderEngine->acquireResources();
    // let the rendering converge for some frames (if specified in the rendering config, we use that number)
    int accumulation_frames = renderer->getTargetAccumulationFrames();
    auto texture = renderEngine->renderFrames({.accumulation_samples=static_cast<size_t>(accumulation_frames > 0 ? accumulation_frames : 300)});
    if(texture == nullptr) {
        Logger(Error) << "internal rendering error";
        return RET_RENDER_ERROR;
    }
    if(export_texture(texture.get(), args.screenshot_output_file) != RET_SUCCESS) {
        Logger(Error) << "image export error";
        return RET_EXPORT_ERROR;
    }
    texture.reset();
    renderEngine->releaseResources();

    return RET_SUCCESS;
}

static const std::string OUT_DIR = "./render_test/";
static const std::vector<VolcaniteArgs> RENDERING_TEST_CONFIGS = {
        {.brick_size=32, .encoding_mode=NIBBLE_ENC, .screenshot_output_file=OUT_DIR + "nibble_32.png"},
           {.brick_size=64, .encoding_mode=DOUBLE_TABLE_RANS_ENC, .operation_mask=(OP_ALL | OP_USE_OLD_PAL_D_BIT), .screenshot_output_file=OUT_DIR + "rANSd_64_old-delta.png"},
        {.cache_palettized=true, .brick_size=64, .encoding_mode=SINGLE_TABLE_RANS_ENC, .screenshot_output_file=OUT_DIR + "rANSd_64_cache-palette.png"},
        {.stream_lod=true, .brick_size=16, .encoding_mode=DOUBLE_TABLE_RANS_ENC, .screenshot_output_file=OUT_DIR + "rANS_16_stream-lod.png"},
        {.cache_mode=CACHE_NOTHING, .brick_size=16, .encoding_mode=NIBBLE_ENC, .operation_mask=(OP_ALL_WITHOUT_STOP & OP_ALL_WITHOUT_DELTA), .random_access=true, .screenshot_output_file=OUT_DIR + "nibble_16_ra.png"},
        {.cache_mode=CACHE_BRICKS, .decode_from_shared_memory=true, .brick_size=64, .encoding_mode=HUFFMAN_WM_ENC, .operation_mask=OP_ALL_WITHOUT_DELTA, .random_access=true,  .screenshot_output_file=OUT_DIR + "hWM_64_ra_cache-brck-sm.png"},
        {.cache_mode=CACHE_VOXELS, .empty_space_resolution=2u, .brick_size=16, .encoding_mode=HUFFMAN_WM_ENC, .operation_mask=OP_ALL_WITHOUT_DELTA, .random_access=true, .screenshot_output_file=OUT_DIR + "hWM_16_ra_cache-voxl_ess.png"},
        {.cache_mode=CACHE_NOTHING, .brick_size=32, .encoding_mode=HUFFMAN_WM_ENC, .operation_mask=OP_ALL_WITHOUT_DELTA, .random_access=true, .screenshot_output_file=OUT_DIR + "hWM_32_ra_cache-none.png"},
    };

glm::vec4 CIE_rgb2xyz(const glm::vec4& rgba) {
    static const glm::mat3 rgb2xyz = glm::mat3(0.4887180f, 0.1762044f, 0.0000000f,
                                               0.3106803f, 0.8129847f, 0.0102048f,
                                               0.2006017f, 0.0108109f, 0.9897952f);
    return {rgb2xyz * glm::vec3(rgba), rgba.a};
}

/// Returns the RMSE between two images, computed in CIE XYZ color space.
/// \param path1 first image
/// \param path2 second image
/// \param threshold average absolute error below which a pixel is ignored in the RMSE in [0,1]
/// \return 0 for equality, negative values for image loading errors, otherwise the RMSE of the images.
double computeImageRMSE(const std::string& path1, const std::string& path2, float threshold = 0.f) {
    int w1, h1, c1, w2, h2, c2;
    unsigned char* image1 = stbi_load(path1.c_str(), &w1, &h1, &c1, STBI_rgb_alpha);
    unsigned char* image2 = stbi_load(path2.c_str(), &w2, &h2, &c2, STBI_rgb_alpha);
    if (image1 == nullptr || image2 == nullptr)
        return -2ll;
    if (w1 != w2 || h1 != h2 || c1 != c2 || c1 != 4) {
        return -1ll;
    }

    size_t element_count = w1 * h1 * c1;
    double rmse = 0.;
    #pragma omp parallel for default(none) shared(element_count, image1, image2, threshold) reduction(+ : rmse)
    for (size_t i = 0; i < element_count; i += 4) {
        glm::vec4 rgba1 = glm::vec4{image1[i], image1[i+1], image1[i+2], image1[i+3]} / 255.f;
        glm::vec4 rgba2 = glm::vec4{image2[i], image2[i+1], image2[i+2], image2[i+3]} / 255.f;

        // convert to CIE XYZ and compute component differences
        glm::vec4 error = glm::abs(CIE_rgb2xyz(rgba1) - CIE_rgb2xyz(rgba2));

        // if a pixel error is over threshold, count it in the RMSE and create a difference image in image1
        if ((error.r + error.g + error.b + error.a) / 4.f > threshold) {
            rmse += glm::dot(error, error);
            glm::ivec4 diff = glm::clamp(glm::ivec4(glm::abs(rgba1 - rgba2) * 255.f), glm::ivec4(0), glm::ivec4(255));
            image1[i + 0] = static_cast<unsigned char>(diff.r);
            image1[i + 1] = static_cast<unsigned char>(diff.g);
            image1[i + 2] = static_cast<unsigned char>(diff.b);
            image1[i + 3] = 255; // static_cast<unsigned char>(diff.a);
        } else {
            image1[i + 0] = 0;
            image1[i + 1] = 0;
            image1[i + 2] = 0;
            image1[i + 3] = 0;
        }
    }

    // normalize for RMSE
    rmse = glm::sqrt(rmse / static_cast<double>(w1 * h1));

    // export a difference image if errors were found
    if (rmse > 0.) {
        std::string diff_image_out = path1;
        diff_image_out.erase(diff_image_out.rfind('.'), 4);
        diff_image_out.append("_DIFF_");
        diff_image_out.append(path2.substr(path2.rfind('/')+1));
        Logger(Debug) << "writing difference image " << absolute(std::filesystem::path(diff_image_out));
        stbi_write_png(diff_image_out.c_str(), w1, h1, c1,
                       reinterpret_cast<const void*>(image1), w1 * c1);
    }

    stbi_image_free(image1);
    stbi_image_free(image2);
    return rmse;
}


/// Renders one image with the same rendering config for different CSGV encoding and decoding modes using the Headless
/// renderer. All output images are compared for differences. The encoding and decoding properties should not change
/// anything in the converged output frame significantly.
int main() {
    // initialize data paths to shaders
    vvv::Paths::initPaths(DATA_DIRS);

    // create headless rendering engine with GPU context

    // create dummy segmentation volume
    glm::uvec3 dim = {133, 70, 194};
    const auto volume = createDummySegmentationVolume({.dim=dim, .seed=0xABCDE12345});

    // create compressed segmentation volume
    std::shared_ptr<CompressedSegmentationVolume> csgv = std::make_shared<CompressedSegmentationVolume>();
    std::shared_ptr<volcanite::CSGVDatabase> csgvDatabase = std::make_shared<volcanite::CSGVDatabase>();
    csgvDatabase->createDummy();
    size_t freq[32];

    // for all test configurations: export one render image each
    for (const auto& args : RENDERING_TEST_CONFIGS) {
        if(!args.screenshot_output_file.ends_with(".png")) {
            Logger(Error) << "must provide export file path for render test run as '*.png'";
            return RET_INVALID_ARG;
        }
        Logger(Info) << "Rendering output " << args.screenshot_output_file;

        // compress the volume
        csgv->clear();
        if (args.encoding_mode == SINGLE_TABLE_RANS_ENC || args.encoding_mode == DOUBLE_TABLE_RANS_ENC) {
            // obtain frequency table(s)
            csgv->setCompressionOptions64(args.brick_size, NIBBLE_ENC, args.operation_mask, args.random_access);
            csgv->compressForFrequencyTable(volume->dataConst(), dim, freq, 2, args.encoding_mode == DOUBLE_TABLE_RANS_ENC, false);
        }
        csgv->setCompressionOptions64(args.brick_size, args.encoding_mode, args.operation_mask, args.random_access, freq, freq + 16);
        csgv->compress(volume->dataConst(), dim, false);
        // possibly separate the detail level-of-detail in the csgv if detail streaming is requested
        if(args.stream_lod && !csgv->isUsingSeparateDetail()) {
            csgv->separateDetail();
        }

        if (!csgv->testLOD(volume->dataConst(), dim))
            return RET_COMPR_ERROR;

        // render one output image
        int ret = renderImageToFile(csgv, csgvDatabase, args);
        if (ret != RET_SUCCESS)
            return ret;
    }

    // check output image files for pair-wise equality
    std::map<std::string, int> error_count;
    int max_id_string_length = 0;
    for (const auto& args : RENDERING_TEST_CONFIGS) {
        error_count[args.screenshot_output_file] = 0;
        max_id_string_length = glm::max(static_cast<int>(args.screenshot_output_file.length()), max_id_string_length);
    }
    Logger(Debug) << "----------------";
    int result = RET_SUCCESS;
    for (int img_a = 0; img_a < RENDERING_TEST_CONFIGS.size(); img_a++) {
        for (int img_b = img_a + 1; img_b < RENDERING_TEST_CONFIGS.size(); img_b++) {
            double rmse = computeImageRMSE(RENDERING_TEST_CONFIGS[img_a].screenshot_output_file,
                                           RENDERING_TEST_CONFIGS[img_b].screenshot_output_file, 0.01);
            if (rmse < 0.) {
                Logger(Error) << "Image loading error for "
                              << RENDERING_TEST_CONFIGS[img_a].screenshot_output_file << " and "
                              << RENDERING_TEST_CONFIGS[img_b].screenshot_output_file;
                error_count[RENDERING_TEST_CONFIGS[img_a].screenshot_output_file]++;
                error_count[RENDERING_TEST_CONFIGS[img_b].screenshot_output_file]++;
                result = RET_RENDER_ERROR;
            } else if (rmse >= 0.01) {
                Logger(Error) << "Rendering differences with RMSE of " << rmse
                              << " for images " << RENDERING_TEST_CONFIGS[img_a].screenshot_output_file << " and "
                              << RENDERING_TEST_CONFIGS[img_b].screenshot_output_file;
                result = RET_RENDER_ERROR;
                error_count[RENDERING_TEST_CONFIGS[img_a].screenshot_output_file]++;
                error_count[RENDERING_TEST_CONFIGS[img_b].screenshot_output_file]++;
            } else {
                Logger(Debug) << RENDERING_TEST_CONFIGS[img_a].screenshot_output_file << " and "
                                        << RENDERING_TEST_CONFIGS[img_b].screenshot_output_file << " ok (RMSE " << rmse << ")";
            }
        }
    }

    Logger(Debug) << "Pair-Wise Comparison Error Counts:";
    for (const auto& args : RENDERING_TEST_CONFIGS)
        Logger(Debug) << fmt::vformat("{:" + std::to_string(max_id_string_length) + "}", fmt::make_format_args(args.screenshot_output_file))
                           << "  " << error_count[args.screenshot_output_file];
    Logger(Debug) << ((result == RET_SUCCESS) ? "  success" : "  errors");
    return result;
}
