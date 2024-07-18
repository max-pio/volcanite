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

#include <glm/glm.hpp>
#include <stdexcept>

using namespace vvv;

namespace volcanite {

    /**
     * Contains lists of 2D pixel indices in an image grid so that the power of two strata are guaranteed to receive one sample after a given power of two number of samples was distributed.
     * Each NxN sequence contains all 2D indices of an NxN image exactly once. The length of the sequence is (N*N) and no point is contained twice.
     *
     * I.e. the first point is always {0,0}. The next (4-1)=3 points put samples in the bottom left corners of the remaining three of the four strata that one receives when splitting the image in half in both dimensions.
     * The next (16-4) points put one sample each in the bottom left corner of all strata given by diving the image resolution by 4 in each dimension and so on.
     **/
    class PixelSequence {
    public:
        static constexpr int halton1x1[1][2]  = {{0,0}};
        static constexpr int halton2x2[4][2]  = {{0,0}, {1,0}, {1,1}, {0,1}};
        static constexpr int halton4x4[16][2] = {{0,0}, {2,0}, {2,2}, {0,2},
                                                 {0,1}, {1,1}, {1,0}, {3,0},
                                                 {3,1}, {2,1}, {3,2}, {3,3},
                                                 {2,3}, {1,3}, {1,2}, {0,3}};
        static constexpr int halton8x8[64][2] = {{0,0}, {4,0}, {4,4}, {0,4},
                                                 {0,2}, {2,2}, {2,0}, {6,0},
                                                 {6,2}, {4,2}, {6,4}, {6,6},
                                                 {4,6}, {2,6}, {2,4}, {0,6},
                                                 {1,0}, {1,1}, {0,1}, {0,3},
                                                 {1,3}, {1,2}, {2,3}, {3,3},
                                                 {3,2}, {3,1}, {2,1}, {3,0},
                                                 {4,1}, {5,1}, {5,0}, {7,0},
                                                 {7,1}, {6,1}, {7,2}, {7,3},
                                                 {6,3}, {5,3}, {5,2}, {4,3},
                                                 {4,5}, {5,5}, {5,4}, {7,4},
                                                 {7,5}, {6,5}, {7,6}, {7,7},
                                                 {6,7}, {5,7}, {5,6}, {4,7},
                                                 {3,7}, {2,7}, {3,6}, {3,5},
                                                 {3,4}, {2,5}, {1,5}, {1,4},
                                                 {0,5}, {1,6}, {1,7}, {0,7}};

        typedef const int (*pixel_sequence_ptr)[2];
        static pixel_sequence_ptr haltonNxN(int power_of_two) {
            switch(power_of_two) {
                case 0:
                    return PixelSequence::halton1x1;
                case 1:
                    return PixelSequence::halton2x2;
                case 2:
                    return PixelSequence::halton4x4;
                case 3:
                    return PixelSequence::halton8x8;
                default:
                    throw std::runtime_error("Cannot provide stratified pixel sequence for power-of-two greater than 3");
            }
        }

        static const glm::ivec2* asVec(const int sequence[][2]) { return reinterpret_cast<const glm::ivec2*>(sequence); };
        static const glm::ivec2* haltonNxNVec(int power_of_two) { return asVec(haltonNxN(power_of_two)); }
    };

} // namespace volcanite
