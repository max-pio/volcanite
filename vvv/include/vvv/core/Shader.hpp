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

// ToDo: Stolen from https://github.com/MomentsInGraphics/vulkan_renderer/blob/c85c087d0aca99f060818b69f9cbbdffb45c33d8/src/vulkan_basics.h

#pragma once
#include "preamble.hpp"

#include "SPIRV-Reflect/spirv_reflect.h"
#include <vvv/config.hpp>
#include <vvv/util/Logger.hpp>

#include <filesystem>

namespace vvv {

//! Handles all information needed to compile a shader into a module
struct GlslShaderRequest {
    //! A path to the file with the GLSL source code
    std::filesystem::path shader_file_path;
    //! The director(ies) which are searched for includes
    std::vector<std::filesystem::path> include_paths;
    //! The name of the function that serves as entry point
    std::string entry_point = "main";
    //! A single bit from VkShaderStageFlagBits to indicate the targeted shader
    //! stage
    vk::ShaderStageFlagBits stage;
    //! A list of strings providing the defines, either as "IDENTIFIER" or
    //! "IDENTIFIER=VALUE". Do not use white space, these strings go into the
    //! command line unmodified.
    std::vector<std::string> defines = {};
    //! A debug label for the shader
    std::string label = "";

    // allow use in std::map<GlslShaderRequest,T>
    auto operator<=>(const GlslShaderRequest&) const = default;
};

//! Handles all information needed to compile a shader into a module.
//! Simplified version of `ShaderRequest`. The shader filename and includes
//! within are relative to the default shader directroy. The stage is
//! derived from the file extension.
struct SimpleGlslShaderRequest {
    //! path relative to the shader include directory
    std::string filename;
    std::vector<std::string> defines = {};
    std::string label = "";
};

struct DescriptorSetLayout {
    uint32_t set_number;
    vk::DescriptorSetLayoutCreateInfo create_info;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
};

struct DescriptorBinding {
    uint32_t set_number;
    vk::DescriptorSetLayoutBinding binding;
    const SpvReflectDescriptorBinding* spirv_binding;
};

struct DescriptorLocation {
    uint32_t set_number;
    uint32_t binding_number;
};

class ShaderCompileError : public std::runtime_error {
public:
    ShaderCompileError(const GlslShaderRequest& request, const std::filesystem::path& spirvPath, int returnValue, const std::string& errorText, const std::string& cmd)
        : request(request), spirvPath(spirvPath), returnValue(returnValue), errorText(errorText), cmd(cmd),
          runtime_error("Compilation of shader " + request.shader_file_path.filename().string() + "failed") { }

    GlslShaderRequest request;
    std::filesystem::path spirvPath;
    int returnValue;
    std::string errorText;
    std::string cmd;
};

enum class ShaderCompileErrorCallbackAction {
    THROW,
    USE_PREVIOUS_CODE
};

using ShaderCompileErrorCallback = std::function<ShaderCompileErrorCallbackAction(const ShaderCompileError&)>;


//! Bundles a Vulkan shader module with its SPIRV code
// TODO: currently assumes a single entry point, which does not work for ray tracing shaders.
// probably best to add an optional argument to every reflection api method that specifies the entry point
// and chooses the default/first entry point if not given.
struct Shader {

    //! The size of the compiled SPIRV code in bytes
    size_t spirv_size;
    //! The compiled SPIRV code
    void *spirv_code;

    std::string label;

    explicit Shader(const GlslShaderRequest& req, const ShaderCompileErrorCallback& compileErrorCallback = nullptr);
    explicit Shader(const SimpleGlslShaderRequest& req, const ShaderCompileErrorCallback& compileErrorCallback = nullptr);

    explicit Shader(size_t spirv_size, void *spirv_code, std::string label = "") : spirv_size(spirv_size), spirv_code(spirv_code), label(label) { reflectShader(); }

    explicit Shader(std::string &filename) : Shader(SimpleGlslShaderRequest{.filename = filename}) {}
    Shader(std::string filename, std::vector<std::string> defines) : Shader(SimpleGlslShaderRequest{.filename = filename, .defines = defines}) {}
    Shader(std::string filename, std::vector<std::string> defines, std::string label) : Shader(SimpleGlslShaderRequest{.filename = filename, .defines = defines, .label = label}) {}

    vk::PipelineShaderStageCreateInfo *pipelineShaderStageCreateInfo(vvv::GpuContextPtr ctx);
    vk::ShaderModule shaderModule(vvv::GpuContextPtr ctx);

    // TODO(Reiner): call this in the destructor
    void destroyModule(vk::Device device) {
        if (m_shaderModule != static_cast<decltype(m_shaderModule)>(nullptr)) {
            device.destroy(m_shaderModule);
            m_shaderModule = nullptr;
        }

        if(spirv_code) {
            free(spirv_code);
            spirv_code = nullptr;
        }
    }

    std::vector<DescriptorSetLayout> reflectDescriptorLayouts() const;

    /**
     * Get the workgroup size by inspecting the shader source.
     *
     * Beware: this will falsely return 1x1x1 if the workgroup size is configured through
     * specialization constants or if the shader is not a compute shader.
     */
    vk::Extent3D reflectWorkgroupSize() const;

    vk::ShaderStageFlagBits reflectShaderStage() const;
    const char *const reflectEntryPointName() const;

    std::optional<DescriptorBinding> reflectBindingByName(const std::string& name) const;

    spv_reflect::ShaderModule const *const rawReflect() const { return m_reflection.get(); }

    std::optional<SpvReflectDescriptorBinding const *const> tryRawReflectBindingByName(std::string name) const {
        auto result = reflectBindingByName(name);
        if (result.has_value())
            return result.value().spirv_binding;
        else
            return std::nullopt;
    }

    SpvReflectDescriptorBinding const *const rawReflectBindingByName(const std::string& name) const {

        auto v = tryRawReflectBindingByName(name);

        if (!v) {
            throw std::runtime_error("binding with name <" + name + "> does not exist in shader <" + label + ">.");
        }

        return v.value();
    }

    std::optional<SpvReflectInterfaceVariable const *const> tryRawReflectOutputByName(const std::string& name) const {
        uint32_t count = 0;
        auto ret = m_reflection->EnumerateOutputVariables(&count, nullptr);
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectInterfaceVariable *> outs(count);
        ret = m_reflection->EnumerateOutputVariables(&count, outs.data());
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);

        for (const auto out : outs) {
            if (strcmp(out->name, name.c_str()) == 0) // || (out->type_description->type_name != nullptr && strcmp(out->type_description->type_name, name.c_str()) == 0)) {
                return out;
        }

        return std::nullopt;
    }

    SpvReflectInterfaceVariable const *const rawReflectOutputByName(const std::string& name) const {
        auto v = tryRawReflectOutputByName(name);
        if (!v) {
            throw std::runtime_error("output with name <" + name + "> does not exist in shader <" + label + ">.");
        }
        return v.value();
    }

    std::vector<SpvReflectInterfaceVariable*> reflectOutputs() const {
        uint32_t count = 0;
        auto ret = m_reflection->EnumerateOutputVariables(&count, nullptr);
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectInterfaceVariable*> outs(count);
        ret = m_reflection->EnumerateOutputVariables(&count, outs.data());
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);
        return outs;
    }


    // TODO: it's not that simple to reflect vertex input because the layout of the bindings / vertex data on the host side is indifferent to the layout locations inside vertex shaders
    std::optional<SpvReflectInterfaceVariable const *const> tryRawReflectInputByName(const std::string& name) const {
        uint32_t count = 0;
        auto ret = m_reflection->EnumerateInputVariables(&count, nullptr);
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectInterfaceVariable *> outs(count);
        ret = m_reflection->EnumerateInputVariables(&count, outs.data());
        assert(ret == SPV_REFLECT_RESULT_SUCCESS);

        for (const auto out : outs) {
            if (strcmp(out->name, name.c_str()) == 0) // || (out->type_description->type_name != nullptr && strcmp(out->type_description->type_name, name.c_str()) == 0)) {
                return out;
        }

        return std::nullopt;
    }

    SpvReflectInterfaceVariable const *const rawReflectInputByName(const std::string& name) const {
        auto v = tryRawReflectInputByName(name);
        if (!v) {
            throw std::runtime_error("input with name <" + name + "> does not exist in shader <" + label + ">.");
        }
        return v.value();
    }

private:
    void createShader(const GlslShaderRequest& request, const ShaderCompileErrorCallback& compileErrorCallback = nullptr);
    [[nodiscard]] static std::filesystem::path compileGlslShader(const GlslShaderRequest& request);
    void loadSpirvFromFile(const std::filesystem::path& path);
    void reflectShader();

    std::optional<std::filesystem::path> getPrecompiledLocalSpirvPath(const SimpleGlslShaderRequest& request);


    vk::ShaderModule m_shaderModule = nullptr;
    std::unique_ptr<vk::PipelineShaderStageCreateInfo> m_shaderStageCreateInfo = nullptr;
    std::unique_ptr<spv_reflect::ShaderModule> m_reflection = nullptr;
};

void setShaderIncludeDirectory(std::string v);
std::string const &getShaderIncludeDirectory();

}
