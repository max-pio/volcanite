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

#include "vvv/volren/BuiltinTransferFunctions.hpp"
#include "vvv/core/GeneralPurposeRenderer.hpp"
#include "vvv/core/Shader.hpp"
#include "vvv/volren/TransferFunction2D.hpp"
#include "vvv/volren/Volume.hpp"
#include "vvv/volren/VolumeHistogram.hpp"
#include "vvv/registration/pushpull.hpp"
#include "vvvwindow/App.hpp"

#include <pybind11/pybind11.h>

#include <pybind11/functional.h> //< enable automatic type conversions for closures
#include <pybind11/iostream.h>   //< allows us to redirect stdout and stderr
#include <pybind11/numpy.h>      //< enable automatic type conversions for numpy
#include <pybind11/operators.h>  //< enables the `py::self` syntax
#include <pybind11/stl.h>        //< enable automatic type conversions for stl containers

namespace py = pybind11;
using namespace pybind11::literals;

template <typename T> void declare_homogenous_volume(py::module &m, std::string templatestr) {
    std::string pyclass_name = std::string("HomogenousCube") + templatestr;
    py::class_<HomogenousCube<T>, Volume<T>, std::shared_ptr<HomogenousCube<T>>>(m, pyclass_name.c_str(), py::buffer_protocol()).def(py::init<T>()).def(py::init<uint32_t, uint32_t, uint32_t, T>());
}

template <typename T> void declare_volume(py::module &m, std::string templatestr) {
    std::string pyclass_name = std::string("Volume") + templatestr;
    // TODO(Reiner): looks like inheritance cannot be combined with the buffer protocol, the following fails:
    // py::class_<Volume, Texture>(m, "Volume", py::buffer_protocol())
    // But it could also be that we are just missing some default constructor, or something else...
    // TODO(Reiner): support all types
    py::class_<Volume<T>, std::shared_ptr<Volume<T>>>(m, pyclass_name.c_str(), py::buffer_protocol())
        .def_static("load_ome_tiff", &Volume<T>::load_ome_tiff)
        .def("getDataInRowMajorOrder", &Volume<T>::getDataInRowMajorOrder, py::return_value_policy::reference)
        .def("getRawData", &Volume<T>::getRawData, py::return_value_policy::reference)
        .def("getElement", py::overload_cast<size_t, size_t, size_t>(&Volume<T>::getElement, py::const_))
        .def("setElement", py::overload_cast<size_t, size_t, size_t, T>(&Volume<T>::setElement))
        .def_readwrite("physical_size_x", &Volume<T>::physical_size_x)
        .def_readwrite("physical_size_y", &Volume<T>::physical_size_y)
        .def_readwrite("physical_size_z", &Volume<T>::physical_size_z)
        .def_readwrite("dim_x", &Volume<T>::dim_x)
        .def_readwrite("dim_y", &Volume<T>::dim_y)
        .def_readwrite("dim_z", &Volume<T>::dim_z)
        .def_buffer([](Volume<T> &m) -> py::buffer_info {
            return py::buffer_info(m.getDataInRowMajorOrder(),         /* Pointer to buffer */
                                   sizeof(T),                          /* Size of one scalar */
                                   py::format_descriptor<T>::format(), /* Python struct-style format descriptor */
                                   3,                                  /* Number of dimensions */
                                   {m.dim_x, m.dim_y, m.dim_z},        /* Buffer dimensions */
                                   {
                                       sizeof(T), /* Strides (in bytes) for each index */
                                       sizeof(T) * m.dim_x,
                                       sizeof(T) * m.dim_x * m.dim_y,
                                   });
        });
    ;
}

PYBIND11_MODULE(pyvvv_core, m) {
    m.doc() = R"pbdoc(
        VVV Python Bindings
        -----------------------

        .. currentmodule:: pyvvv_core

        .. autosummary::
           :toctree: _generate
    )pbdoc";

    /**
     * Allows you to redirect stdout and stdcerr to the output of a jupyter notebook as follows:
     *
     * ```
     * import vvv
     *
     * // setup application. See `notebooks/template.ipynb` for full code
     *
     * with vvv.ostream_redirect(stdout=True, stderr=True):
     *      app.exec()
     * ```
     *
     * Beware: the formatter in jupyter notebooks is quite slow. Redirecting vulkan debug messages
     * as shown above will bring your system to a crawl when lots of debug messages are emitted.
     */
    py::add_ostream_redirect(m, "ostream_redirect");

    m.def("getShaderIncludeDirectory", &getShaderIncludeDirectory);
    m.def("setShaderIncludeDirectory", &setShaderIncludeDirectory);
    m.def("getDataDirectory", &vvv::getDataDirectory);
    m.def("setDataDirectory", &vvv::setDataDirectory);
    m.def("createDefaultDebugUtilities", &createDefaultDebugUtilities);
    m.def("createDefaultApplication", &createDefaultApplication);

    py::class_<RendererOutput>(m, "RendererOutput").def_property("texture", &RendererOutput::getTexture, nullptr);

    // TODO(Reiner): generate the vulkan bindings from the HPP header
    py::class_<vk::CommandBuffer>(m, "vkCommandBuffer");
    py::class_<vk::Instance>(m, "vkInstance");
    py::class_<vk::Device>(m, "vkDevice");
    py::class_<vk::PhysicalDevice>(m, "vkPhysicalDevice");
    py::class_<vk::Extent2D>(m, "vkExtent2D").def(py::init<uint32_t, uint32_t>()).def_readwrite("width", &vk::Extent2D::width).def_readwrite("height", &vk::Extent2D::height);

    py::class_<GpuContext>(m, "GpuContext")
        .def("getInstance", &GpuContext::getInstance)
        .def("getDevice", &GpuContext::getDevice)
        .def("getPhysicalDevice", &GpuContext::getPhysicalDevice)
        .def("getQueueFamilyIndices", &GpuContext::getQueueFamilyIndices)
        .def("getScreenExtent", &GpuContext::getScreenExtent)
        .def("getCamera", &GpuContext::getCamera);

    py::class_<Application, GpuContext>(m, "Application")
        .def(py::init<std::string, std::shared_ptr<Renderer>, std::shared_ptr<DebugUtilities>>())
        .def("acquireResources", &Application::acquireResources)
        .def("releaseResources", &Application::releaseResources)
        .def("areResourcesAcquired", &Application::areResourcesAcquired)
        .def("enableInstanceLayer", &Application::enableInstanceLayer)
        .def("enableInstanceLayer", &Application::enableInstanceLayer)
        .def("enableInstanceExtension", &Application::enableInstanceExtension)
        .def("setVSync", &Application::setVSync)
        .def("exec", &Application::exec)
        .def("execAsync", &Application::execAsync)
        .def("swapChainImageCount", &Application::swapChainImageCount)
        .def("currentSwapChainImageIndex", &Application::currentSwapChainImageIndex)
        .def("maximalInFlightFrameCount", &Application::maximalInFlightFrameCount)
        .def("currentInFlightFrameIndex", &Application::currentInFlightFrameIndex)
        .def("getScreenExtent", &Application::getScreenExtent);

    py::class_<DebugUtilities>(m, "DebugUtilities")
        .def("isEnabled", &DebugUtilities::isEnabled)
        .def("isExtensionSupported", &DebugUtilities::isExtensionSupported)
        .def("extensionName", &DebugUtilities::extensionName);

    py::class_<DebugUtilsExt, DebugUtilities>(m, "DebugUtilsExt").def_readonly_static("ExtensionName", &DebugUtilsExt::ExtensionName).def(py::init<>());
    py::class_<DebugMarkerExt, DebugUtilities>(m, "DebugMarkerExt").def_readonly_static("ExtensionName", &DebugMarkerExt::ExtensionName).def(py::init<>());
    py::class_<DebugNoop, DebugUtilities>(m, "DebugNoop").def(py::init<>());

    py::class_<Renderer, std::shared_ptr<Renderer>>(m, "Renderer")
        .def("renderNextFrame", &Renderer::renderNextFrame)
        .def("initResources", &Renderer::initResources)
        .def("releaseResources", &Renderer::releaseResources)
        .def("initSwapchainResources", &Renderer::initSwapchainResources)
        .def("releaseSwapchain", &Renderer::releaseSwapchain);

    py::class_<GeneralPurposeRenderer, Renderer, std::shared_ptr<GeneralPurposeRenderer>>(m, "GeneralPurposeRenderer")
        .def(py::init<>())
        .def("mostRecentFrame", &GeneralPurposeRenderer::mostRecentFrame)
        .def("performCommands", py::overload_cast<const std::function<void(vk::CommandBuffer)> &, vk::CommandBuffer>(&GeneralPurposeRenderer::performCommands, py::const_))
        .def("performCommands", py::overload_cast<const std::function<void(vk::CommandBuffer)> &>(&GeneralPurposeRenderer::performCommands, py::const_))
        .def("setTransferFunction", &GeneralPurposeRenderer::setTransferFunction)
        .def("setVolume", &GeneralPurposeRenderer::setVolume)
        .def("getDeviceVolume", &GeneralPurposeRenderer::getDeviceVolume)
        .def("getHostVolume", &GeneralPurposeRenderer::getHostVolume);

    py::class_<StagingBuffer, std::shared_ptr<StagingBuffer>>(m, "StagingBuffer")
        .def(py::init<GpuContext const *const, vk::BufferUsageFlags, size_t>())
        .def(py::init<GpuContext const *const, size_t>())
        .def("download", &StagingBuffer::download);

    py::class_<Texture, std::shared_ptr<Texture>>(m, "Texture")
        .def_readonly("width", &Texture::width)
        .def_readonly("height", &Texture::height)
        .def_readonly("depth", &Texture::depth)
        .def("setName", &Texture::setName)
        .def("upload", &Texture::upload)
        .def("memorySize", py::overload_cast<vk::ImageAspectFlags>(&Texture::memorySize, py::const_))
        .def("memorySize", py::overload_cast<>(&Texture::memorySize, py::const_))
        .def("capture", py::overload_cast<vk::CommandBuffer, StagingBuffer const &, vk::PipelineStageFlags>(&Texture::capture))
        .def("capture", py::overload_cast<vk::CommandBuffer, StagingBuffer const &>(&Texture::capture))
        .def("capture", py::overload_cast<vk::CommandBuffer, vk::PipelineStageFlags>(&Texture::capture))
        .def("capture", py::overload_cast<vk::CommandBuffer>(&Texture::capture));

    py::enum_<ChannelOpacityState>(m, "ChannelOpacityState").value("AlphaPremultiplied", ChannelOpacityState::AlphaPremultiplied).value("PostMultiplied", ChannelOpacityState::PostMultiplied);

    py::class_<DiscreteTransferFunction, std::shared_ptr<DiscreteTransferFunction>>(m, "DiscreteTransferFunction")
        .def(py::init<GpuContext const *const, std::vector<uint16_t>, ChannelOpacityState>())
        .def("texture", &DiscreteTransferFunction::texture)
        .def("data", &DiscreteTransferFunction::data, py::return_value_policy::reference)
        .def("upload", &DiscreteTransferFunction::upload)
        .def("hasPostprocess", &DiscreteTransferFunction::hasPostprocess)
        .def("postprocess", &DiscreteTransferFunction::postprocess);

    py::class_<VectorTransferFunction, std::shared_ptr<VectorTransferFunction>>(m, "VectorTransferFunction")
        .def(py::init<std::vector<float>, std::vector<float>>())
        .def_readonly_static("linearOpacityRamp", &VectorTransferFunction::linearOpacityRamp)
        .def_readonly_static("fullyOpaque", &VectorTransferFunction::fullyOpaque)
        .def("rasterize", py::overload_cast<GpuContext const *const, size_t>(&VectorTransferFunction::rasterize, py::const_))
        .def("rasterize", py::overload_cast<size_t>(&VectorTransferFunction::rasterize, py::const_))
        .def("sampleOpacity", &VectorTransferFunction::sampleOpacity)
        .def("sampleRgb", &VectorTransferFunction::sampleRgb);

    py::class_<TransferFunction2dOptions>(m, "TransferFunction2dOptions")
        .def(py::init<>())
        .def_readwrite("countSamplesScalarValue", &TransferFunction2dOptions::countSamplesScalarValue)
        .def_readwrite("countSamplesGradientMagnitude", &TransferFunction2dOptions::countSamplesGradientMagnitude)
        .def_readwrite("backgroundOpacity", &TransferFunction2dOptions::backgroundOpacity)
        .def_readwrite("foregroundOpacity", &TransferFunction2dOptions::foregroundOpacity)
        .def_readwrite("feathering", &TransferFunction2dOptions::feathering);

    py::class_<TransferFunction2D, DiscreteTransferFunction, std::shared_ptr<TransferFunction2D>>(m, "TransferFunction2D")
        .def(py::init<GpuContext const *const, const uint16_t *const, size_t, ChannelOpacityState, TransferFunction2dOptions>(), "ctx"_a, "values"_a, "countValues"_a,
             "channelOpacityState"_a = DefaultChannelOpacityState, "options"_a = DefaultTransferFunction2dOptions)
        .def(py::init<GpuContext const *const, std::vector<uint16_t>, ChannelOpacityState, TransferFunction2dOptions>(), "ctx"_a, "values"_a, "channelOpacityState"_a = DefaultChannelOpacityState,
             "options"_a = DefaultTransferFunction2dOptions)
        .def("texture2d", &TransferFunction2D::texture2d, py::return_value_policy::reference)
        .def("addPolygon", &TransferFunction2D::addPolygon)
        .def("setFeathering", &TransferFunction2D::setFeathering)
        .def("setBackgroundOpacity", &TransferFunction2D::setBackgroundOpacity)
        .def("setForegroundOpacity", &TransferFunction2D::setForegroundOpacity);

    py::class_<PreintegrationOptions>(m, "PreintegrationOptions")
        .def(py::init<>())
        .def_readwrite("slabCount", &PreintegrationOptions::slabCount)
        .def_readwrite("stepCount", &PreintegrationOptions::stepCount);

    py::class_<DiscretePreintegratedTransferFunction, DiscreteTransferFunction, std::shared_ptr<DiscretePreintegratedTransferFunction>>(m, "DiscretePreintegratedTransferFunction")
        .def(py::init<GpuContext const *const, const uint16_t *const, size_t, ChannelOpacityState, PreintegrationOptions>(), "ctx"_a, "values"_a, "countValues"_a,
             "channelOpacityState"_a = DefaultChannelOpacityState, "preintegrationOptions"_a = DefaultPreintegrationOptions)
        .def(py::init<GpuContext const *const, std::vector<uint16_t>, ChannelOpacityState, PreintegrationOptions>(), "ctx"_a, "values"_a, "channelOpacityState"_a = DefaultChannelOpacityState,
             "preintegrationOptions"_a = DefaultPreintegrationOptions)
        .def("preintegratedLookupTable", &DiscretePreintegratedTransferFunction::preintegratedLookupTable, py::return_value_policy::reference);

    py::class_<VolumeHistogramOptions>(m, "VolumeHistogramOptions")
        .def(py::init<>())
        .def_readwrite("gradientLimits", &VolumeHistogramOptions::gradientLimits)
        .def_readwrite("valueLimits", &VolumeHistogramOptions::valueLimits)
        .def_readwrite("countGradientBuckets", &VolumeHistogramOptions::countGradientBuckets)
        .def_readwrite("countScalarValueBuckets", &VolumeHistogramOptions::countScalarValueBuckets);

    py::class_<GpuVolume, std::shared_ptr<GpuVolume>>(m, "GpuVolume");

    py::class_<VolumeHistogram>(m, "VolumeHistogram")
        .def(py::init<GpuContext const *const, std::shared_ptr<GpuVolume>, VolumeHistogramOptions>(), "ctx"_a, "volume"_a, "histogramOptions"_a = DefaultVolumeHistogramOptions)
        .def("prepare", &VolumeHistogram::prepare)
        .def("executeCommands", &VolumeHistogram::executeCommands)
        .def("getGradientLimits", &VolumeHistogram::getGradientLimits)
        .def("getScalarValueLimits", &VolumeHistogram::getScalarValueLimits)
        .def("texture", &VolumeHistogram::texture);

    declare_volume<uint16_t>(m, "_uint16");
    declare_volume<float>(m, "_float");
    declare_homogenous_volume<uint16_t>(m, "_uint16");
    declare_homogenous_volume<float>(m, "_float");

    m.def("pushpull", &pushpull);

    m.attr("buildDatetimeISO8601") = vvv::build_time_iso8601;
    m.attr("__version__") = vvv::project_version;

#include "colormaps.cpp"

#if defined(NDEBUG)
    m.attr("buildIsDebug") = false;
#else
    m.attr("buildIsDebug") = true;
#endif
}
