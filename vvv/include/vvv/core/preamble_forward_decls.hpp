#pragma once

// and some headers that we want to be present everywhere.
#include <iostream> // get cout, cerr for debugging
#include <cstdint> // for `uint32_t` etc

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <vvv/vk/destroy.hpp>

#include <vvv/util/util.hpp>

// forward declare some core classes and typedefs to resolve cyclic dependencies
namespace vvv {
class Synchronization;
class DebugUtilities;
class GpuContext;
typedef GpuContext const &GpuContextRef;
typedef GpuContext const *const GpuContextPtr;
typedef GpuContext *const GpuContextRwPtr;
class PassBase;
class PassCompute; // TODO(Max) can we remove forward declarations for PassCompute and PassGraphics if PassBase is listed here?
class SinglePassGraphics;
}; // namespace vvv