//  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <vvv/volren/tf/VectorTransferFunction.hpp>

const std::vector<float> vvv::VectorTransferFunction::linearOpacityRamp = {0.0, 0.0, 1.0, 1.0};
const std::vector<float> vvv::VectorTransferFunction::fullyOpaque = {0.0, 1.0, 1.0, 1.0};