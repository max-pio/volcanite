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

#include <vvv/core/preamble_forward_decls.hpp>

#include <vvv/core/Buffer.hpp>
#include <vvv/core/MultiBuffering.hpp>
#include <vvv/core/Shader.hpp>

#include <SPIRV-Reflect/spirv_reflect.h>

#include <typeinfo>

namespace vvv {

namespace details {

static bool is_spvr_matrix(SpvReflectBlockVariable *m) {
    // note, matrices do also have the vector bit set
    // return m->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX;
    return m->type_description->op == SpvOpTypeMatrix;
}

static bool is_spvr_matrix_shape(SpvReflectBlockVariable *m, uint32_t cols, uint32_t rows) {
    return m->numeric.matrix.column_count == cols && m->numeric.matrix.row_count == rows;
}

static bool is_spvr_vec_shape(SpvReflectBlockVariable *m, uint32_t components) {
    return m->numeric.vector.component_count == components;
}

static bool is_spvr_vec(SpvReflectBlockVariable *m) {
    // note, matrices do also have the vector bit set
    // return (ty->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) &&
    //       !(ty->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
    //    ;
    return m->type_description->op == SpvOpTypeVector;
}

static bool is_spvr_component_width(SpvReflectBlockVariable *ty, uint32_t w) {
    return ty->type_description->traits.numeric.scalar.width == w;
}

static bool is_spvr_component_signed(SpvReflectBlockVariable *ty) {
    return ty->type_description->traits.numeric.scalar.signedness == 1;
}

static bool is_spvr_component_unsigned(SpvReflectBlockVariable *ty) {
    return ty->type_description->traits.numeric.scalar.signedness == 0;
}

static bool is_spvr_component_bool(SpvReflectBlockVariable *ty) {
    return ty->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_BOOL;
}

static bool is_spvr_component_int_or_uint(SpvReflectBlockVariable *ty) {
    return ty->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT;
}

static bool is_spvr_component_float(SpvReflectBlockVariable *ty) {
    return ty->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT;
}

template <typename T>
static bool is_spvr_type(SpvReflectBlockVariable *ty) { throw std::runtime_error("unimplemented typecheck"); }

template <>
bool is_spvr_type<float &>(SpvReflectBlockVariable *ty) {
    return ty->type_description->op == SpvOpTypeFloat;
}

template <>
bool is_spvr_type<bool &>(SpvReflectBlockVariable *ty) {
    return ty->type_description->op == SpvOpTypeBool;
}

template <>
bool is_spvr_type<int32_t &>(SpvReflectBlockVariable *ty) {
    return ty->type_description->op == SpvOpTypeInt && is_spvr_component_signed(ty) && is_spvr_component_width(ty, 32);
}
template <>
bool is_spvr_type<uint32_t &>(SpvReflectBlockVariable *ty) {
    return ty->type_description->op == SpvOpTypeInt && is_spvr_component_unsigned(ty) && is_spvr_component_width(ty, 32);
}

template <>
bool is_spvr_type<glm::mat4x4 &>(SpvReflectBlockVariable *m) {
    return is_spvr_matrix(m) && is_spvr_component_float(m) && is_spvr_matrix_shape(m, 4, 4);
}

template <>
bool is_spvr_type<glm::mat3x4 &>(SpvReflectBlockVariable *m) {
    return is_spvr_matrix(m) && is_spvr_component_float(m) && is_spvr_matrix_shape(m, 3, 4);
}

template <>
bool is_spvr_type<glm::mat3x3 &>(SpvReflectBlockVariable *m) {
    return is_spvr_matrix(m) && is_spvr_component_float(m) && is_spvr_matrix_shape(m, 3, 3);
}

template <>
bool is_spvr_type<glm::vec4 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_float(m) && is_spvr_vec_shape(m, 4);
}

template <>
bool is_spvr_type<glm::vec3 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_float(m) && is_spvr_vec_shape(m, 3);
}

template <>
bool is_spvr_type<glm::vec2 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_float(m) && is_spvr_vec_shape(m, 2);
}

template <>
bool is_spvr_type<glm::ivec4 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_signed(m) && is_spvr_vec_shape(m, 4);
}

template <>
bool is_spvr_type<glm::ivec3 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_signed(m) && is_spvr_vec_shape(m, 3);
}

template <>
bool is_spvr_type<glm::ivec2 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_signed(m) && is_spvr_vec_shape(m, 2);
}

template <>
bool is_spvr_type<glm::uvec4 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_unsigned(m) && is_spvr_vec_shape(m, 4);
}

template <>
bool is_spvr_type<glm::uvec3 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_unsigned(m) && is_spvr_vec_shape(m, 3);
}
template <>
bool is_spvr_type<glm::uvec2 &>(SpvReflectBlockVariable *m) {
    return is_spvr_vec(m) && is_spvr_component_int_or_uint(m) && is_spvr_component_unsigned(m) && is_spvr_vec_shape(m, 2);
}

template <typename T>
static void memcpy_type(SpvReflectBlockVariable *member, char *uniformset, T *value) {
    // memory layout in shader matches memory layout in host

    if (sizeof(T) != member->size) {
        // if you hit this exception, you need to implement a copy routine that inserts the correct padding!
        std::ostringstream err;
        err << "memory layout of <" << member->name << "> on host does not match memory layout in shader." << std::endl
            << "if you hit this exception, you need to implement a template specialization of `memcpy_type` that inserts the correct padding!";
        throw std::runtime_error(err.str());
    }

    memcpy(uniformset + member->offset, value, sizeof(T));
}

template <>
void memcpy_type<glm::mat3>(SpvReflectBlockVariable *member, char *uniformset, glm::mat3 *value) {
    // mat3 is column major, each column is padded from vec3 to a vec4, so we have to insert padding after
    // every 3 members
    auto mat_offset = uniformset + member->offset;
    auto unpadded_col_size = reinterpret_cast<char *>(&(*value)[1]) - reinterpret_cast<char *>(&(*value)[0]); // sizeof(glm::mat3::col_type); //member->numeric.matrix.row_count * (member->numeric.scalar.width / bitToByte);
    for (int col = 0; col < member->numeric.matrix.column_count; col++) {
        memcpy(mat_offset + col * member->numeric.matrix.stride, reinterpret_cast<char *>(value) + col * unpadded_col_size, unpadded_col_size);
    }
}

}; // namespace details

/// Let's you work with uniform sets without first creating a CPP struct through a stringly-typed API.
class UniformReflected {
  public:
    explicit UniformReflected(const SpvReflectDescriptorBinding *const binding) : m_dirty({}), m_data(binding->block.size), m_binding(binding) {}
    // there is block.member_count and block.members
    // there is alternatively block.type_description.members and block.type_description.member_count
    // but each member also has a block.members[i].type_description

    template <typename T>
    void setUniform(std::string memberName, T value) {
        const auto member = getMember(memberName);

        // sanity checks
        if (!details::is_spvr_type<T &>(member)) {
            std::ostringstream err;
            // this stringification of T does only work with certain compilers
            err << "type mismatch for <" << memberName << ">. Host expected <" << typeid(T).name() << ">, but shader has other.";
            throw std::runtime_error(err.str());
        }

        details::memcpy_type<T>(member, m_data.data(), &value);
        m_dirty.assign(m_dirty.size(), true);
    }

    /// Get a pointer to the CPU data region of a uniform member for writing. Note that you still have to mark the region as dirty and call upload, or call forceUpload!
    template <typename T>
    T *getUniformPtr(std::string memberName) {
        const auto member = getMember(memberName);

        // sanity checks
        if (sizeof(T) != member->size || !details::is_spvr_type<T &>(member)) {
            std::ostringstream err;
            // this stringification of T only works with certain compilers
            err << "type mismatch for <" << memberName << ">. Host expected <" << typeid(T).name() << ">, but shader has other.";
            throw std::runtime_error(err.str());
        }

        return reinterpret_cast<T *>(m_data.data() + member->offset);
    }

    DescriptorLocation getLocation() const { return {.set_number = m_binding->set, .binding_number = m_binding->binding}; }

    size_t getByteSize() const { return m_binding->block.size; }

    [[nodiscard]] size_t getCopies() const { return m_dataGpu.size(); }

    void createGpuBuffers(GpuContextPtr ctx, size_t copies) {
        assert((copies > 0 && copies == m_dataGpu.size() && ctx == m_dataGpu[0]->getCtx()) || m_dataGpu.size() == 0);

        m_dataGpu.resize(copies, nullptr);
        m_dirty.resize(copies, true);
        for (int idx = 0; idx < copies; ++idx) {
            m_dataGpu[idx] = std::make_shared<Buffer>(ctx, BufferSettings{.label = m_binding->name,
                                                                          .byteSize = getByteSize(),
                                                                          .usage = vk::BufferUsageFlagBits::eUniformBuffer,
                                                                          .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
        }
    }

    void createGpuBuffers(GpuContextPtr ctx) {
        const auto windowing = ctx->getWsi();
        createGpuBuffers(ctx, windowing ? windowing->maximalInFlightFrameCount() : 1);
    }

    void createGpuBuffer(GpuContextPtr ctx) { createGpuBuffers(ctx, 1); }

    void upload(uint32_t idx = 0) {
        if (!m_dirty[idx]) {
            return;
        }
        forceUpload(idx);
    }

    void forceUpload(uint32_t idx = 0) {
        m_dirty[idx] = false;
        m_dataGpu[idx]->upload(m_data);
    }

    [[nodiscard]] std::shared_ptr<Buffer> getGpuBuffer(uint32_t idx = 0) const { return m_dataGpu[idx]; }
    [[nodiscard]] const std::vector<std::shared_ptr<Buffer>> &getGpuBuffers() const { return m_dataGpu; }

  private:
    SpvReflectBlockVariable *getMember(std::string memberName) {
        for (int i = 0; i < m_binding->block.member_count; ++i) {
            const auto &member = m_binding->block.members + i;

            if (strcmp(member->name, memberName.c_str()) == 0) {
                return member;
            }
        }

        Logger(Error) << "unkown member <" + memberName + "> in uniform block" << m_binding->name;
        throw std::runtime_error("unknown member <" + memberName + ">");
    }

    /// @deprecated use MultiBuffering on Wsi instead
    template <typename T>
    using MultiBuffered = std::vector<T>;

    std::vector<char> m_data;
    MultiBuffered<std::shared_ptr<Buffer>> m_dataGpu;
    const SpvReflectDescriptorBinding *const m_binding;
    MultiBuffered<bool> m_dirty;
};

std::shared_ptr<UniformReflected> reflectUniformSet(vvv::GpuContextPtr ctx, vk::ArrayProxy<const std::shared_ptr<Shader>> shaders, const std::string &name);

}; // namespace vvv