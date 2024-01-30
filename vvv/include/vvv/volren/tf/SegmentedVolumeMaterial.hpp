#pragma once

#include <vvv/volren/tf/builtin.hpp>

namespace vvv {

class SegmentedVolumeMaterial {
public:
    char name[32] = "";
    int discrAttribute = 0;                 // discriminator attribute used to determine which labels belong to the material
    glm::vec2 discrInterval = {0.f, 1.f};   // labels with the discrAttribute within this interval belong to the material
    int tfAttribute = 0;
    std::shared_ptr<VectorTransferFunction> tf = std::make_shared<vvv::VectorTransferFunction>(colormaps::grayscale);
    glm::vec2 tfMinMax = {0.f, 1.f};
};

}
