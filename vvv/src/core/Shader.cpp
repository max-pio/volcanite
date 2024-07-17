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

#include <vvv/core/Shader.hpp>

#include <vvv/util/Paths.hpp>

#include <sstream>
#include <string>

#include <cstdio>
#include <cstdlib>

#include <SPIRV-Reflect/spirv_reflect.h>

namespace vvv {
/// Returns the standardized name for the given shader stage, e.g. "vert" or "frag". Only one bit of
/// VkShaderStageFlagBits can be set in the input.
std::string get_shader_stage_name(vk::ShaderStageFlagBits stage) {
    switch (stage) {
    case vk::ShaderStageFlagBits::eVertex:
        return "vert";
    case vk::ShaderStageFlagBits::eTessellationControl:
        return "tesc";
    case vk::ShaderStageFlagBits::eTessellationEvaluation:
        return "tese";
    case vk::ShaderStageFlagBits::eGeometry:
        return "geom";
    case vk::ShaderStageFlagBits::eFragment:
        return "frag";
    case vk::ShaderStageFlagBits::eCompute:
        return "comp";
    case vk::ShaderStageFlagBits::eRaygenKHR:
        return "rgen";
    case vk::ShaderStageFlagBits::eIntersectionKHR:
        return "rint";
    case vk::ShaderStageFlagBits::eAnyHitKHR:
        return "rahit";
    case vk::ShaderStageFlagBits::eClosestHitKHR:
        return "rchit";
    case vk::ShaderStageFlagBits::eMissKHR:
        return "rmiss";
    case vk::ShaderStageFlagBits::eCallableKHR:
        return "rcall";
    case vk::ShaderStageFlagBits::eTaskNV:
        return "task";
    case vk::ShaderStageFlagBits::eMeshNV:
        return "mesh";
    default:
        std::ostringstream err;
        err << "Unsupported shared stage " << to_string(stage);
        throw std::runtime_error(err.str());
    };
}

const vk::ShaderStageFlagBits get_shader_stage(const std::string& stage) {
    if (stage == "vert")
        return vk::ShaderStageFlagBits::eVertex;
    if (stage == "tesc")
        return vk::ShaderStageFlagBits::eTessellationControl;
    if (stage == "tese")
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    if (stage == "geom")
        return vk::ShaderStageFlagBits::eGeometry;
    if (stage == "frag")
        return vk::ShaderStageFlagBits::eFragment;
    if (stage == "comp")
        return vk::ShaderStageFlagBits::eCompute;
    if (stage == "rgen")
        return vk::ShaderStageFlagBits::eRaygenKHR;
    if (stage == "rint")
        return vk::ShaderStageFlagBits::eIntersectionKHR;
    if (stage == "rahit")
        return vk::ShaderStageFlagBits::eAnyHitKHR;
    if (stage == "rchit")
        return vk::ShaderStageFlagBits::eClosestHitKHR;
    if (stage == "rmiss")
        return vk::ShaderStageFlagBits::eMissKHR;
    if (stage == "rcall")
        return vk::ShaderStageFlagBits::eCallableKHR;
    if (stage == "task")
        return vk::ShaderStageFlagBits::eTaskNV;
    if (stage == "mesh")
        return vk::ShaderStageFlagBits::eMeshNV;

    std::ostringstream err;
    err << "Unable to reflect shared stage from file suffix <." << stage << ">";
    throw std::runtime_error(err.str());
}

std::map<GlslShaderRequest, std::filesystem::path> alreadyCompilesSpirvFiles;

#define USE_PRECOMPILED_LOCAL_SPIRV

Shader::Shader(const SimpleGlslShaderRequest& req, const ShaderCompileErrorCallback& compileErrorCallback) {
#ifdef USE_PRECOMPILED_LOCAL_SPIRV
    // try to load a precompiled spirv file from a data path
    auto local_spirv = getPrecompiledLocalSpirvPath(req);
    if(local_spirv.has_value()) {
        loadSpirvFromFile(local_spirv.value());
        Logger(INFO) << "Loaded " << local_spirv.value().string();
        reflectShader();
        return;
    }
#endif

    auto path = Paths::findShaderPath(req.filename);

    GlslShaderRequest request{.shader_file_path = path,
                              .include_paths = Paths::getShaderDirectories(),
                              .entry_point = "main",
                              .stage = get_shader_stage(path.extension().string().substr(1)),
                              .defines = req.defines,
                              .label = req.label};

    createShader(request, compileErrorCallback);
}

Shader::Shader(const GlslShaderRequest& req, const ShaderCompileErrorCallback& compileErrorCallback) {
#ifdef USE_PRECOMPILED_LOCAL_SPIRV
    Logger(WARN) << "Cannot load precompiled shaders for non-simple GlslShaderRequests";
#endif
    createShader(req, compileErrorCallback);
}

std::optional<std::filesystem::path> Shader::getPrecompiledLocalSpirvPath(const SimpleGlslShaderRequest& request) {
    // find out which name a spirv file for this request would have
    std::string filename = request.filename;
    {
        // to support long file names that would occur if we store many compile parameters (like defines) as plain text
        // in the file name, we compute a hash of all those parameters.
        size_t compile_hash = 0;
        // hash all defines
        for (const std::string &define : request.defines) {
            // old way to do it:
            // spirv_path += '_';
            // for (char c : define)
            //     spirv_path += (c >= '0' && c <= '9' || c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z') ? c : '_';
            for (char c : define) {
                compile_hash = std::hash<char>{}(c) ^ (std::rotl<size_t>(compile_hash, 1));
            }
        }
        // here would be the place to add other compile time things to the hash
        // ..
        if (compile_hash != 0)
            filename += "_" + std::to_string(compile_hash);
        filename += ".spv";
    }

    // spirv files are expected to be in a spv subfolder of any data path.
    // any '/' for indicating subfolder is replaced with a '_' to concatenate a single filename.
    std::filesystem::path path = std::filesystem::path("spv") / filename;

    // try to find this spirv file within the data paths
    if (!Paths::hasDataPath(path.string())) {
        return {};
    }
    else {
        return Paths::findDataPath(path.string());
    }
}

void Shader::createShader(const GlslShaderRequest& request, const ShaderCompileErrorCallback& compileErrorCallback) {
        std::filesystem::path spirvPath;

        Logger(DEBUG) << request.shader_file_path;
        try {
            spirvPath = compileGlslShader(request);
        }
        catch(ShaderCompileError& e) {

            auto callback = compileErrorCallback ? compileErrorCallback : ShaderCompileErrorCallback([](const ShaderCompileError& e) {
                Logger(ERROR) << "Compilation of shader " << e.request.shader_file_path.filename() << " failed.\n\n"
                              << "Command line: " << e.cmd << "\n"
                              << "Return value: " << e.returnValue << "\n"
                              << "\n" << e.errorText;
                return ShaderCompileErrorCallbackAction::THROW;
            });

            auto action = callback(e);

            if (action == ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE) {
                if (!alreadyCompilesSpirvFiles.contains(request))
                    throw std::runtime_error("Cannot reuse old shader source for " + e.request.shader_file_path.string() + " as it has not yet been compiled.");

                spirvPath = alreadyCompilesSpirvFiles[request];
            }
            else if (action == ShaderCompileErrorCallbackAction::THROW) {
                throw; // rethrow this exception
            }
            else throw std::runtime_error("ShaderCompileErrorCallbackAction not defined.");
        }

        loadSpirvFromFile(spirvPath);

        reflectShader();
}

std::filesystem::path Shader::compileGlslShader(const GlslShaderRequest& request) {

    // Verify that the shader file exists using std::filesystem
    if (!is_regular_file(request.shader_file_path)) {
        std::ostringstream err;
        err << "The shader file at path " << request.shader_file_path << " does not exist or cannot be opened";
        throw std::runtime_error(err.str());
    }

    auto spirv_path = Paths::getTempFileForDataPath(request.shader_file_path);

    // to support long file names that would occur if we store many compile parameters (like defines) as plain text
    // in the file name, we compute a hash of all those parameters.
    size_t compile_hash = 0;
    // hash all defines
    for (const std::string &define : request.defines) {
            // old way to do it:
            // spirv_path += '_';
            // for (char c : define)
            //     spirv_path += (c >= '0' && c <= '9' || c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z') ? c : '_';
            for (char c : define) {
                compile_hash = std::hash<char>{}(c) ^ (std::rotl<size_t>(compile_hash, 1));
            }
    }
    // here would be the place to add other compile time things to the hash
    // ..
    if(compile_hash != 0)
        spirv_path += "_" + std::to_string(compile_hash);
    spirv_path += ".spv";

    std::ostringstream cmd;

    // TODO(Reiner): escape things
    cmd << vvv::shader_compiler_executable
        << " --client vulkan100"                                 // Vulkan SPIR-V semantics
        << " --target-env spirv1.5"                              // 1.5 is default for vulkan 1.2
        << " --quiet"                                            // supress output of currently compiling file
        << " -S " << get_shader_stage_name(request.stage); // select shader stage

    for (const auto &flag : request.defines) {
        cmd << " -D\"" << flag << "\"";
    }
#ifdef NDEBUG
    cmd << " -DNDEBUG";
#endif

    for (auto& path : request.include_paths) {
        cmd << " -I\"" << path.string() << "\" ";
    }

    cmd << " --entry-point " << request.entry_point
        << " -o \"" << spirv_path.string() << "\" "
        << " \"" << request.shader_file_path.string() << "\"";

    std::string cmd_output;
    char read_buffer[1024];

#ifdef _WIN32
    FILE *cmd_stream = _popen(cmd.str().c_str(), "r");
    while (fgets(read_buffer, sizeof(read_buffer), cmd_stream))
        cmd_output += read_buffer;
    int cmd_ret = _pclose(cmd_stream);
#else
    FILE* cmd_stream = popen(cmd.str().c_str(), "r");
    while (fgets(read_buffer, sizeof(read_buffer), cmd_stream))
        cmd_output += read_buffer;
    int cmd_ret = pclose(cmd_stream);
#endif

    if (cmd_ret != 0) {
        throw ShaderCompileError(request, spirv_path, cmd_ret, cmd_output, cmd.str());
    }

    alreadyCompilesSpirvFiles[request] = spirv_path;

    Logger(DEBUG) << "Compiled " << spirv_path;
    return spirv_path;
}

void Shader::loadSpirvFromFile(const std::filesystem::path& path) {
    FILE *file = fopen(path.string().c_str(), "rb");
    if (!file) {
        throw std::runtime_error("Could not open SPIRV file " + path.string());
    }

    // Read the SPIR-V code from the file
    if (fseek(file, 0, SEEK_END) || (spirv_size = ftell(file)) < 0) {
        fclose(file);
        std::ostringstream err;
        err << "failed to determine file size";
        throw std::runtime_error(err.str());
    }

    spirv_code = malloc(spirv_size);
    fseek(file, 0, SEEK_SET);
    spirv_size = fread(spirv_code, sizeof(char), spirv_size, file);
    fclose(file);
}

vk::ShaderModule Shader::shaderModule(vvv::GpuContextPtr ctx) {

    if (m_shaderModule != static_cast<vk::ShaderModule>(nullptr)) {
        return m_shaderModule;
    }

    vk::ShaderModuleCreateInfo module_info({}, spirv_size, static_cast<uint32_t *>(spirv_code));
    m_shaderModule = ctx->getDevice().createShaderModule(module_info);

    if (!label.empty()) {
        ctx->debugMarker->setName(m_shaderModule, label);
    }

    return m_shaderModule;
}

vk::PipelineShaderStageCreateInfo *Shader::pipelineShaderStageCreateInfo(vvv::GpuContextPtr ctx) {
    if (m_shaderStageCreateInfo != nullptr) {
        return m_shaderStageCreateInfo.get();
    }

    m_shaderStageCreateInfo = std::make_unique<vk::PipelineShaderStageCreateInfo>(vk::PipelineShaderStageCreateFlags(), reflectShaderStage(), shaderModule(ctx), reflectEntryPointName());
    return m_shaderStageCreateInfo.get();
}

void Shader::reflectShader() {
    m_reflection = std::make_unique<spv_reflect::ShaderModule>(spirv_size, spirv_code);
    assert(m_reflection->GetResult() == SPV_REFLECT_RESULT_SUCCESS);
}

std::vector<DescriptorSetLayout> Shader::reflectDescriptorLayouts() const {

    uint32_t count = 0;
    SpvReflectResult result;
    result = m_reflection->EnumerateDescriptorSets(&count, nullptr);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    std::vector<SpvReflectDescriptorSet *> sets(count);
    result = m_reflection->EnumerateDescriptorSets(&count, sets.data());
    assert(result == SPV_REFLECT_RESULT_SUCCESS);


    std::vector<DescriptorSetLayout> set_layouts(sets.size(), DescriptorSetLayout{});

    for (size_t i_set = 0; i_set < sets.size(); ++i_set) {
        const SpvReflectDescriptorSet &refl_set = *(sets[i_set]);
        DescriptorSetLayout &layout = set_layouts[i_set];
        layout.bindings.resize(refl_set.binding_count);
        for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding) {
            const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);
            VkDescriptorSetLayoutBinding &layout_binding = static_cast<VkDescriptorSetLayoutBinding &>(layout.bindings[i_binding]);
            layout_binding.binding = refl_binding.binding;
            layout_binding.descriptorType = static_cast<VkDescriptorType>(refl_binding.descriptor_type);
            layout_binding.descriptorCount = 1;
            for (uint32_t i_dim = 0; i_dim < refl_binding.array.dims_count; ++i_dim) {
                layout_binding.descriptorCount *= refl_binding.array.dims[i_dim];
            }
            layout_binding.stageFlags = static_cast<VkShaderStageFlagBits>(reflectShaderStage());
        }
        layout.set_number = refl_set.set;
        layout.create_info.bindingCount = refl_set.binding_count;
        layout.create_info.pBindings = layout.bindings.data();
    }

    return set_layouts;
}

std::optional<DescriptorBinding> Shader::reflectBindingByName(const std::string& name) const {
    uint32_t count = 0;
    SpvReflectResult result;
    result = m_reflection->EnumerateDescriptorSets(&count, nullptr);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    std::vector<SpvReflectDescriptorSet *> sets(count);
    result = m_reflection->EnumerateDescriptorSets(&count, sets.data());
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    // the name struct name of uniforms is in paranthesis if `struct NAME {};` is used instead of
    // `struct SOME_IDENTIFIER {} NAME;`
    std::string enclosed_name = "(" + name + ")";

    for (size_t i_set = 0; i_set < sets.size(); ++i_set) {
        const SpvReflectDescriptorSet &refl_set = *(sets[i_set]);
        for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding) {
            const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);

            // type name is used for uniform names
            auto type_name = refl_binding.type_description->type_name;
            if (refl_binding.name == name || refl_binding.name == enclosed_name || (type_name != nullptr && type_name == name)) {
                DescriptorBinding binding;
                binding.binding.binding = refl_binding.binding;
                binding.binding.descriptorType = static_cast<vk::DescriptorType>(refl_binding.descriptor_type);
                binding.binding.descriptorCount = 1;
                for (uint32_t i_dim = 0; i_dim < refl_binding.array.dims_count; ++i_dim) {
                    binding.binding.descriptorCount *= refl_binding.array.dims[i_dim];
                }
                binding.set_number = refl_set.set;
                binding.spirv_binding = &refl_binding;

                return binding;
            }
        }
    }

    return std::nullopt;
}

vk::Extent3D Shader::reflectWorkgroupSize() const {
    auto entrypoint = spvReflectGetEntryPoint(&m_reflection->GetShaderModule(), reflectEntryPointName());
    assert(entrypoint != nullptr);

    return vk::Extent3D(entrypoint->local_size.x, entrypoint->local_size.y, entrypoint->local_size.z);
}

vk::ShaderStageFlagBits Shader::reflectShaderStage() const { return static_cast<vk::ShaderStageFlagBits>(m_reflection->GetShaderStage()); }

const char *const Shader::reflectEntryPointName() const { return m_reflection->GetEntryPointName(); }

std::string _shader_include_dir = vvv::default_shader_include_dir;

void setShaderIncludeDirectory(std::string v) { _shader_include_dir = v; }
std::string const &getShaderIncludeDirectory() { return _shader_include_dir; }

}
