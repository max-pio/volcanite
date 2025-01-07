#include "volcanite/util/segmentation_volume_synthesis.hpp"

#include <ranges>
#include "vvv/volren/Volume.hpp"
#include "vvv/util/Logger.hpp"

using namespace vvv;

namespace volcanite {

std::shared_ptr<Volume<uint32_t>> createDummySegmentationVolume(SyntheticSegmentationVolumeCfg cfg) {
    Logger(INFO) << "Creating synthetic segmentation volume with dimension " << str(cfg.dim);

    if (glm::any(glm::greaterThan(cfg.min_region_dim, cfg.max_region_dim)))
        throw std::invalid_argument("Synthetic segmentation volume minimum region dimensions must be smaller than"
                                    " or equal to the maximum region dimensions.");
    if (glm::any(glm::equal(cfg.min_region_dim, glm::uvec3{0u}))
        || glm::any(glm::equal(cfg.max_region_dim, glm::uvec3{0u}))
        || glm::any(glm::equal(cfg.dim, glm::uvec3{0u}))
        || cfg.voxels_per_label == 0u) {
        throw std::invalid_argument("Synthetic segmentation volume dimension, voxels per label, or region dimension"
                                    " must not be 0.");
    }

    std::default_random_engine eng{cfg.seed};
    std::uniform_int_distribution<unsigned int> urd(0u, ~0u);
    #define V_RND_UINT() urd(eng)

    std::srand(cfg.seed);
    std::shared_ptr<Volume<uint32_t>> volume = std::make_shared<Volume<uint32_t>>(cfg.dim[0], cfg.dim[1], cfg.dim[2],
                                                        cfg.dim[0], cfg.dim[1], cfg.dim[2], vk::Format::eR32Uint,
                                                        cfg.dim[0] * cfg.dim[1] * cfg.dim[2]);
    memset(volume->data().data(), 0, cfg.dim[0] * cfg.dim[1] * cfg.dim[2] * sizeof(uint32_t));

    const int number_of_areas = static_cast<int>((cfg.dim[0] * cfg.dim[1] * cfg.dim[2] + 8192u - 1u) / 8192u);
    for (int i = 0; i < number_of_areas; i++) {
        uint32_t label = V_RND_UINT();
        uint32_t w = V_RND_UINT() % 32 + 1;
        uint32_t h = V_RND_UINT() % 32 + 1;
        uint32_t d = V_RND_UINT() % 32 + 1;
        int x_min = static_cast<int>(V_RND_UINT() % cfg.dim[0]) - static_cast<int>(w / 2);
        int y_min = static_cast<int>(V_RND_UINT() % cfg.dim[1]) - static_cast<int>(h / 2);
        int z_min = static_cast<int>(V_RND_UINT() % cfg.dim[2]) - static_cast<int>(d / 2);

        #pragma omp parallel for collapse(3) default(none) shared(x_min, y_min, z_min, w, h, d, label, volume, cfg)
        for (int z = z_min; z < z_min + d; z++) {
            for (int y = y_min; y < y_min + h; y++) {
                for (int x = x_min; x < x_min + w; x++) {
                    if (x < 0 || y < 0 || z < 0 || x >= cfg.dim[0] || y >= cfg.dim[1] || z >= cfg.dim[2])
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
    if (!descr.starts_with("#synth"))
        throw std::invalid_argument("Synthetic volume descriptor must start with #synth");

    SyntheticSegmentationVolumeCfg cfg;

    std::set<unsigned char> processed = {};

    for (const auto arg: std::views::split(descr, "_")
                         | std::ranges::views::transform([](auto &&subrange) {
            const auto size = std::ranges::distance(subrange);
            return size ? std::string_view(&*subrange.begin(), size) : std::string_view();
        })) {

        std::stringstream ss;
        ss << arg;
        unsigned char c;

        if (arg == "#synth") {
            continue;
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
        } else {
            if (processed.contains('_'))
                throw std::invalid_argument("Synthetic volume descriptor contains unknown key");
            processed.insert('_');
            ss >> cfg.dim.x;
            ss >> c; // x
            ss >> cfg.dim.y;
            ss >> c; // x
            ss >> cfg.dim.z;
        }

        if (ss.fail()) {
            std::stringstream err;
            err << "Synthetic volume descriptor " << descr << " contains invalid key " << arg;
            throw std::invalid_argument(err.str());
        }
    }

    return createDummySegmentationVolume(cfg);
}

std::shared_ptr<Volume<uint32_t>> createWorstCaseSegmentationVolume(glm::uvec3 dim) {
    std::shared_ptr<Volume<uint32_t>> volume = std::make_shared<Volume<uint32_t>>(dim[0], dim[1], dim[2],
                                                                                  dim[0], dim[1], dim[2],
                                                                                  vk::Format::eR32Uint,
                                                                                  dim[0] * dim[1] * dim[2]);

    uint32_t* raw_voxels = volume->data().data();
    #pragma omp parallel for default(none) shared(raw_voxels, dim)
    for (size_t v = 0; v < dim[0] * dim[1] * dim[2]; v++) {
        raw_voxels[v] = v;
    }

    return volume;
}

} // namespace volcanite
