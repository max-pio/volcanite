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

#pragma once

#include "vvv/volren/Volume.hpp"

namespace {

    inline uint32_t randomUint() { return std::rand() % (~0u); }

    Volume <uint32_t> createDummySegmentationVolume(glm::uvec3 dim = {100, 100, 100}) {
        Volume <uint32_t> volume = Volume<uint32_t>(1.f, 1.f, 1.f, dim[0], dim[1], dim[2], vk::Format::eR32Uint,
                                                    dim[0] * dim[1] * dim[2]);
        memset(volume.data().data(), 0, dim[0] * dim[1] * dim[2] * sizeof(uint32_t));

        const int number_of_areas = static_cast<int>((dim[0] * dim[1] * dim[2] + 999u) / 8192u);
        for (int i = 0; i < number_of_areas; i++) {
            uint32_t label = randomUint();
            uint32_t w = randomUint() % 32 + 1;
            uint32_t h = randomUint() % 32 + 1;
            uint32_t d = randomUint() % 32 + 1;
            int x_min = static_cast<int>(randomUint() % dim[0]) - w / 2;
            int y_min = static_cast<int>(randomUint() % dim[1]) - h / 2;
            int z_min = static_cast<int>(randomUint() % dim[2]) - d / 2;

            #pragma omp parallel for collapse(3) default(none) shared(x_min, y_min, z_min, w, h, d, label, volume, dim)
            for (int z = z_min; z < z_min + d; z++) {
                for (int y = y_min; y < y_min + h; y++) {
                    for (int x = x_min; x < x_min + w; x++) {
                        if (x < 0 || y < 0 || z < 0 || x >= dim[0] || y >= dim[1] || z >= dim[2])
                            continue;
                        volume.setElement(x, y, z, label);
                    }
                }
            }
        }

        return volume;
    }

}