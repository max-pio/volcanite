// ToDo: compression_synth.cpp executable must be updated, e.g. to support HEADLESS mode

#ifndef HEADLESS

#include <string>
#include "vvv/util/Logger.hpp"
#include "vvvwindow/App.hpp"
#include "vvvwindow/entrypoint.hpp"

// Test the compression result for all LoDs:
//#define RUN_TEST

// Run the interactive renderer after compression:
#define RUN_APP

// Set 'empty' labels in the Big01 data set to zero:
//#define SET_EMPTY_TO_ZERO

// Export statistics to CSV files for later analysis in python:
//#define EXPORT_STATS

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "vvv/volren/Volume.hpp"

using namespace vvv;

inline std::string vec2str(glm::ivec3 v) {
    std::string out = "[";
    for(int i = 0; i < 3 ; i++) {
        if(i > 0)
            out += ", ";
        out += std::to_string(v[i]);
    }
    return out + "]";
}

/// This executable generates a poissoin-like 3D point set from which a 3D voronoi partition is computed.
/// The resulting voluem can be compressed, rendered with the Volcanite backend, or simply exported to a simplified NRRD file.
/// Point set size is controlled by specifying the points per million voxels (which subsequently also is the number of
/// lables in the data set) with the ppm variable. Even though OpenMP is used to parallelize the search, no acceleration
/// structure etc. is used. So be careful with large volume dimensions or ppm sizes.
int compression_synth(int argc, char *argv[]) {
    // configuration -------------------
    int brick_dim = 16;                                                         // size of one brick
    bool force_recompute = false;                                               // do a fresh compression even if there is a precomputed file
    CompressedSegmentationVolume::RANSMode rANS_mode = vvv::CompressedSegmentationVolume::DOUBLE_TABLE_RANS;  // use no rANS, rANS with one table for everything, or rANS with a second freq. table for the finest LoD
    unsigned int frequency_pass_subsampling = 8u;                               // only use every n³th block in every 2nd chunk file for computing frequencies
    bool use_detail_separation = false;                                         // split off the operation stream of the finest LoD for on-demand CPU to GPU streaming
    std::string appName = "Synthetic Data Creator";
    bool vsync = false;
    // ---------------------------------

    // you can iterate over different point densities to test the effect on compression:
    //std::vector<size_t> ppm = {10, 50, 100, 250, 500, 1000};
    std::vector<size_t> ppm = {1000};
    uint32_t w = 128, h = 128, d = 128;
    const size_t candidate_count = 128;
    std::vector<glm::ivec3> candidates(candidate_count);
    std::vector<uint32_t> dist2(candidate_count);
    uint32_t max_candidate;

    std::shared_ptr<Volume<uint32_t>> volume = std::make_shared<Volume<uint32_t>>(1.f, 1.f, 1.f, w, h, d, vk::Format::eR32Uint, w * h * d);
    std::shared_ptr<vvv::CompressedSegmentationVolume> csgvol = std::make_shared<vvv::CompressedSegmentationVolume>();
    for(int ppm_i = 0; ppm_i < ppm.size(); ppm_i++) {
        size_t points_per_million = ppm[ppm_i];
        const size_t point_count = static_cast<size_t>(static_cast<float>(w * h * d) / 1000000.f * static_cast<float>(points_per_million));
        Logger(INFO) << "generating synthetic data set with " << point_count << " points.";
        // generated data (can be reused in a loop)
        std::vector<glm::ivec3> points(point_count);

        // set an export path if VOLCANITE_DEFAULT_DATA_PATH is set
        std::string out_path = VOLCANITE_DEFAULT_DATA_PATH;
        if(!out_path.empty())
            out_path += "/synthetic/voronoi" + std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(d) + "_" + std::to_string(points_per_million) + "lpm.raw";

// #define ssout
#ifdef ssout
        std::stringstream ssx("");
        std::stringstream ssy("");
        std::stringstream ssz("");
#endif

        for (size_t i = 0; i < point_count; i++) {
#pragma omp parallel for default(none) shared(candidates, dist2, candidate_count, w, h, d, points)
            for (size_t n = 0; n < candidate_count; n++) {
                candidates[n].x = std::rand() % static_cast<int>(w);
                candidates[n].y = std::rand() % static_cast<int>(h);
                candidates[n].z = std::rand() % static_cast<int>(d);
                dist2[n] = -1u;
                glm::ivec3 dist;
                uint32_t d2;
                for (const auto &p : points) {
                    dist = p - candidates[n];
                    d2 = dist.x * dist.x + dist.y * dist.y + dist.z * dist.z;
                    if (d2 < dist2[n]) {
                        dist2[n] = d2;
                    }
                }
            }
            max_candidate = 0;
            for (uint32_t n = 1; n < candidate_count; n++) {
                if (dist2[n] > dist2[max_candidate]) {
                    max_candidate = n;
                }
            }
#ifdef ssout
            ssx << candidates[max_candidate].x << ",";
            ssy << candidates[max_candidate].y << ",";
            ssz << candidates[max_candidate].z << ",";
#endif
            points[i] = candidates[max_candidate];
        }
#ifdef ssout
        std::cout << ssx.str() << std::endl;
        std::cout << ssy.str() << std::endl;
        std::cout << ssz.str() << std::endl;
#endif

        Logger(INFO) << "point set created. filling volume..";

// now we have our nice point set. Next step: voronoi
#pragma omp parallel for collapse(3) default(none) shared(points, volume, w, h, d)
        for (int z = 0; z < d; z++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    glm::ivec3 pos(x, y, z);
                    glm::ivec3 dist = pos - points[0];
                    uint32_t d2 = dist.x * dist.x + dist.y * dist.y + dist.z * dist.z;
                    uint32_t min_point = 0;
                    uint32_t min_d2 = d2;
                    for (uint32_t i = 1; i < points.size(); i++) {
                        dist = pos - points[i];
                        d2 = dist.x * dist.x + dist.y * dist.y + dist.z * dist.z;
                        if (d2 < min_d2) {
                            min_point = i;
                            min_d2 = d2;
                        }
                    }
                    volume->setElement(x, y, z, min_point);
                }
            }
        }

        // output volume
        if(!out_path.empty()) {
            Logger(INFO) << "done. exporting to " << out_path;
            std::ofstream fout(out_path, std::ios::out);
            assert(fout.is_open());

            fout << w << " " << h << " " << d << std::endl;
            fout << "uint32" << std::endl;
            fout.write(reinterpret_cast<char *>(volume->data().data()), volume->data().size() * sizeof(uint32_t));
            fout.close();
        }
        else {
            Logger(INFO) << "done. Set VOLCANITE_DEFAULT_DATA_PATH cmake variable to export synthetic data.";
        }
    }

    // perform the compression
    glm::uvec3 volume_dim = glm::uvec3(volume->dim_x, volume->dim_y, volume->dim_z);
    size_t code_frequencies[32];
    if(rANS_mode != vvv::CompressedSegmentationVolume::NO_RANS) {
        csgvol->setCompressionOptions(brick_dim, CompressedSegmentationVolume::NO_RANS);
        csgvol->compressForFrequencyTable(volume->data(), volume_dim, code_frequencies, frequency_pass_subsampling, false);
        // we can't risk missing symbol frequencies >0 in our table due to subsampling
        if(frequency_pass_subsampling > 1u) {
            bool changed = false;
            for(int i = 0; i < 16; i++) {
                if (code_frequencies[i] == 0ul) {
                    changed = true;
                    code_frequencies[i] = 2ul;
                }
                if(rANS_mode == vvv::CompressedSegmentationVolume::DOUBLE_TABLE_RANS && code_frequencies[i+16] == 0ul) {
                    changed = true;
                    code_frequencies[i+16] = 2ul;                }
            }
            if (changed)
                Logger(WARN) << " set zero frequency to 2 to avoid missing symbols because of frequency pass subsampling.";
        }
    }
    csgvol->setCompressionOptions64(brick_dim, rANS_mode, code_frequencies, code_frequencies + 16);
    if (use_detail_separation)
        csgvol->separateDetail();
    csgvol->compress(volume->data(), volume_dim, true);

#ifdef RUN_APP
    // run the interactive Application
    const auto renderer = std::make_shared<vvv::CompressedSegmentationVolumeRenderer>();
    const auto csgv_db = std::make_shared<CSGVDatabase>();
    renderer->setCompressedSegmentationVolume(csgvol, csgv_db);
    auto app = Application::create(appName, renderer);

    // execute app
    app->setVSync(vsync);
    auto returnValue = app->exec();
    // to prevent unordered releasing of resources through destructors, we explicitly call releaseResources
    app->releaseResources();
    return returnValue;
#else
    return 0;
#endif
}

ENTRYPOINT(compression_synth)

#endif // HEADLESS
