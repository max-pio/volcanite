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

#include "volcanite/util/segmentation_volume_synthesis.hpp"

#include "csgv_constants.incl"

#include "glm/glm.hpp"
#include "vvv/util/Logger.hpp"
#include "vvv/volren/Volume.hpp"
#include <ranges>

using namespace vvv;

namespace volcanite {

std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(SyntheticSegmentationVolumeCfg cfg) {

    if (glm::any(glm::greaterThan(cfg.min_region_dim, cfg.max_region_dim)))
        throw std::invalid_argument("Synthetic segmentation volume minimum region dimensions must be smaller than"
                                    " or equal to the maximum region dimensions.");
    if (glm::any(glm::equal(cfg.min_region_dim, glm::uvec3{0u})) || glm::any(glm::equal(cfg.max_region_dim, glm::uvec3{0u})) || glm::any(glm::equal(cfg.dim, glm::uvec3{0u})) || cfg.voxels_per_label == 0u) {
        throw std::invalid_argument("Synthetic segmentation volume dimension, voxels per label, or region dimension"
                                    " must not be 0.");
    }

    std::default_random_engine eng{static_cast<uint_fast32_t>(cfg.seed)};
    std::uniform_int_distribution<unsigned int> urd(0u, ~0u);
#define V_RND_UINT() urd(eng)

    std::srand(cfg.seed);
    std::shared_ptr<Volume<uint32_t>> volume = std::make_shared<Volume<uint32_t>>(cfg.dim[0], cfg.dim[1], cfg.dim[2],
                                                                                  cfg.dim[0], cfg.dim[1], cfg.dim[2], vk::Format::eR32Uint,
                                                                                  cfg.dim[0] * cfg.dim[1] * cfg.dim[2]);
    memset(volume->data().data(), 0, cfg.dim[0] * cfg.dim[1] * cfg.dim[2] * sizeof(uint32_t));

    const size_t number_of_areas = static_cast<size_t>(cfg.dim[0] * cfg.dim[1] * cfg.dim[2] + cfg.voxels_per_label - 1u) / cfg.voxels_per_label;

    Logger(Info) << "Creating synthetic segmentation volume with dimension " << str(cfg.dim)
                 << " and approx. " << number_of_areas << " label regions, " << cfg.voxels_per_label << " voxels/label.";
    for (size_t i = 0; i < number_of_areas; i++) {
        const uint32_t label = V_RND_UINT();
        const uint32_t w = V_RND_UINT() % (cfg.max_region_dim.x - cfg.min_region_dim.x) + cfg.min_region_dim.x;
        const uint32_t h = V_RND_UINT() % (cfg.max_region_dim.y - cfg.min_region_dim.y) + cfg.min_region_dim.y;
        const uint32_t d = V_RND_UINT() % (cfg.max_region_dim.z - cfg.min_region_dim.z) + cfg.min_region_dim.z;
        const int x_min = static_cast<int>(V_RND_UINT() % cfg.dim[0]) - static_cast<int>(w / 2);
        const int y_min = static_cast<int>(V_RND_UINT() % cfg.dim[1]) - static_cast<int>(h / 2);
        const int z_min = static_cast<int>(V_RND_UINT() % cfg.dim[2]) - static_cast<int>(d / 2);
        const float sphere_box_interpolation = (static_cast<float>(V_RND_UINT() % 4096) / 4096.f) + 2.f * (cfg.sphere_box_shape - 0.5f);
        const float sphere_box_dist = sphere_box_interpolation * 0.73205080757f + 1.f;

#pragma omp parallel for collapse(3) default(none) shared(x_min, y_min, z_min, w, h, d, label, volume, cfg, sphere_box_dist)
        for (int z = z_min; z < z_min + d; z++) {
            for (int y = y_min; y < y_min + h; y++) {
                for (int x = x_min; x < x_min + w; x++) {
                    if (x < 0 || y < 0 || z < 0 || x >= cfg.dim[0] || y >= cfg.dim[1] || z >= cfg.dim[2])
                        continue;

                    // spherical:
                    if (length(glm::vec3(static_cast<float>(x_min - x) + static_cast<float>(w / 2u),
                                         static_cast<float>(y_min - y) + static_cast<float>(h / 2u),
                                         static_cast<float>(z_min - z) + static_cast<float>(d / 2u)) /
                               (glm::vec3(w, h, d) / 2.f)) >= sphere_box_dist)
                        continue;
                    volume->setElement(x, y, z, label);
                }
            }
        }
    }

#undef V_RND_UINT

    return volume;
}

std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(std::string_view descr) {
    if (!descr.starts_with(CSGV_SYNTH_PREFIX_STR))
        throw std::invalid_argument("Synthetic volume descriptor must start with +synth");

    SyntheticSegmentationVolumeCfg cfg;

    std::set<unsigned char> processed = {};

    constexpr std::string_view split{"_"};
    for (const auto arg : std::views::split(descr, split) | std::ranges::views::transform([](auto &&subrange) {
                              const auto size = std::ranges::distance(subrange);
                              return size ? std::string_view(&*subrange.begin(), size) : std::string_view();
                          })) {

        if (arg.starts_with("--")) // ignore rest: useful for appending the chunk placeholders +synth_--x{}y{}z{}
            break;

        std::stringstream ss;
        ss << arg;
        unsigned char c;

        if (arg == CSGV_SYNTH_PREFIX_STR) {
            continue;
        } else if (arg.starts_with("d")) {
            if (processed.contains('d'))
                throw std::invalid_argument("Synthetic volume descriptor key r duplicate");
            processed.insert('d');
            ss >> c; // d
            ss >> cfg.dim.x;
            ss >> c; // x
            ss >> cfg.dim.y;
            ss >> c; // x
            ss >> cfg.dim.z;
        } else if (arg.starts_with("l")) {
            if (processed.contains('l'))
                throw std::invalid_argument("Synthetic volume descriptor key l duplicate");
            processed.insert('l');
            ss >> c; // l
            ss >> cfg.voxels_per_label;
        } else if (arg.starts_with("max")) {
            if (processed.contains('m'))
                throw std::invalid_argument("Synthetic volume descriptor key max duplicate");
            processed.insert('m');
            ss >> c >> c >> c; // max
            ss >> cfg.max_label;
        } else if (arg.starts_with("r")) {
            if (processed.contains('r'))
                throw std::invalid_argument("Synthetic volume descriptor key r duplicate");
            processed.insert('r');
            ss >> c; // r
            ss >> cfg.min_region_dim.x;
            ss >> c; // x
            ss >> cfg.min_region_dim.y;
            ss >> c; // x
            ss >> cfg.min_region_dim.z;
            ss >> c; // -
            ss >> cfg.max_region_dim.x;
            ss >> c; // x
            ss >> cfg.max_region_dim.y;
            ss >> c; // x
            ss >> cfg.max_region_dim.z;
        } else if (arg.starts_with("b")) {
            if (processed.contains('b'))
                throw std::invalid_argument("Synthetic volume descriptor key b duplicate");
            processed.insert('b');
            ss >> c; // b
            ss >> cfg.sphere_box_shape;
            cfg.sphere_box_shape = glm::clamp(cfg.sphere_box_shape, 0.f, 1.f);
        } else if (arg.starts_with("s")) {
            if (processed.contains('s'))
                throw std::invalid_argument("Synthetic volume descriptor key s duplicate");
            processed.insert('s');
            ss >> c; // s
            ss >> cfg.seed;
        } else {
            if (processed.contains('_')) {
                std::stringstream err;
                err << "Synthetic volume descriptor " << descr << " contains invalid key " << arg;
                err << ". syntax:\n"
                    << getDummySegmentationVolumeHelpStr();
                throw std::invalid_argument(err.str());
            }
            processed.insert('_');
            ss >> cfg.dim.x;
            ss >> c; // x
            ss >> cfg.dim.y;
            ss >> c; // x
            ss >> cfg.dim.z;
        }

        if (ss.fail()) {
            std::stringstream err;
            err << "Error parsing synthetic volume descriptor. syntax:\n";
            err << getDummySegmentationVolumeHelpStr();
            throw std::invalid_argument(err.str());
        }
    }

    // automatically choose region sizes based on label density if not explicitly set
    if (!processed.contains('r')) {
        double voxel_width = glm::pow(static_cast<double>(cfg.voxels_per_label), 1. / 3.);
        constexpr double min_ratio = 0.75, max_ratio = 1.25;
        cfg.min_region_dim = glm::max(glm::uvec3(static_cast<uint32_t>(voxel_width * min_ratio)), glm::uvec3(1u));
        cfg.max_region_dim = glm::max(glm::uvec3(static_cast<uint32_t>(voxel_width * max_ratio)), cfg.min_region_dim);
    }
    // automatically choose label count if only region size is specified
    else if (!processed.contains('l')) {
        glm::uvec3 avg_reg = glm::max((cfg.min_region_dim + cfg.max_region_dim) / 2u, glm::uvec3(1u));
        cfg.voxels_per_label = avg_reg.x * avg_reg.y * avg_reg.z;
    }

    return createDummySegmentationVolume(cfg);
}

std::shared_ptr<Volume<uint32_t>> createWorstCaseSegmentationVolume(glm::uvec3 dim) {
    std::shared_ptr<Volume<uint32_t>> volume = std::make_shared<Volume<uint32_t>>(dim[0], dim[1], dim[2],
                                                                                  dim[0], dim[1], dim[2],
                                                                                  vk::Format::eR32Uint,
                                                                                  dim[0] * dim[1] * dim[2]);

    uint32_t *raw_voxels = volume->data().data();
#pragma omp parallel for default(none) shared(raw_voxels, dim)
    for (size_t v = 0; v < dim[0] * dim[1] * dim[2]; v++) {
        raw_voxels[v] = v;
    }

    return volume;
}

} // namespace volcanite
