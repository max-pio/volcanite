//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

// header loading the whole library, a `#include <vvv/vvv.hpp>` should be
// enough to start working with the library.

#include <vvv/config.hpp>
#include <vvv/core/GpuContext.hpp>

#include <vvv/core/DefaultGpuContext.hpp>

#include <stb/stb_image.hpp>
#include <stb/stb_image_write.hpp>

#include <vvv/core/Renderer.hpp>
#include <vvv/core/Shader.hpp>

#include <vvv/passes/PassCompute.hpp>
#include <vvv/passes/SinglePassGraphics.hpp>

#include <vvv/volren/Volume.hpp>
#include <vvv/volren/tf/TransferFunction.hpp>
#include <vvv/volren/tf/TransferFunction1D.hpp>
#include <vvv/volren/tf/VectorTransferFunction.hpp>
#include <vvv/volren/tf/builtin.hpp>