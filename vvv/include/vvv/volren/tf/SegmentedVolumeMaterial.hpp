#pragma once

#include <vvv/volren/tf/builtin.hpp>

namespace vvv {

class SegmentedVolumeMaterial {

public:
    static const int DISCR_NONE = -2;  // disabled material
    static const int DISCR_ANY = -1;
    char name[64] = "";
    int discrAttribute = 0;                 // discriminator attribute used to determine which labels belong to the material
    glm::vec2 discrInterval = {0.f, 1.f};   // labels with the discrAttribute within this interval belong to the material
    int tfAttribute = 0;
    std::shared_ptr<VectorTransferFunction> tf = std::make_shared<vvv::VectorTransferFunction>(colormaps::grayscale);
    glm::vec2 tfMinMax = {0.f, 1.f};
    float opacity = 1.f;

    bool isActive() { return discrAttribute > DISCR_NONE; }

    glm::vec2 getDiscrInterval() {
        if(discrAttribute == DISCR_NONE)        // accept none
            return glm::vec2(1.f, 0.f);
        else if(discrAttribute == DISCR_ANY)    // accept all
            return glm::vec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
        else
            return discrInterval;
    }

    int getSafeDiscrAttribute() { return glm::max(0, discrAttribute); } // for NONE and ANY we read from the 0 attribute


};

}
