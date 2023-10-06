#pragma once


// header loading the whole library, a `#include <vvv/vvv.hpp>` should be
// enough to start working with the library.

// TODO(Reiner): move these into a `core` folder/namespace
#include <vvv/core/GpuContext.hpp>
#include <vvv/config.hpp>

#include <vvv/core/DefaultGpuContext.hpp>

#include <stb/stb_image.hpp>
#include <stb/stb_image_write.hpp>

#include <vvv/core/Shader.hpp>
#include <vvv/core/Renderer.hpp>

#include <vvv/passes/PassCompute.hpp>
#include <vvv/passes/SinglePassGraphics.hpp>

#include <vvv/volren/Volume.hpp>
#include <vvv/volren/tf/TransferFunction.hpp>
#include <vvv/volren/tf/TransferFunction1D.hpp>
#include <vvv/volren/tf/VectorTransferFunction.hpp>
#include <vvv/volren/tf/builtin.hpp>