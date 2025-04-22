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

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

namespace vvv {

/// @brief VertexPrimitives can be used to generate glm::vec3 arrays of vertices for creating geometric primitives.
///
/// Primitives have an extension of 1^3 and are centered around (0,0,0) meaning that they lie withing [-0.5, 0.5]^3.
/// transformAll(...) can be used to transform a primitive using a 4x4 transformation matrix.
///
/// ToDos:\n
/// - The usage of glm::vec3 wastes one float element and may have alignment problems. We may use a vector of vec4s or simple floats.\n
/// - These are just vertex vectors. We may use a mesh representation object and/or indexed meshes instead.
class VertexPrimitives {

  public:
    /// Transform all vertices of the given vector with the transformation in place.
    static void transformAll(std::vector<glm::vec3> &vertices, const glm::mat4 transformation) {
        for (glm::vec3 &v : vertices)
            v = glm::vec3(transformation * glm::vec4(v, 1.f));
    }

    static std::vector<glm::vec3> createUVSphereVec3(int tesselation = 8) {
        assert(tesselation >= 2);
        int parallelNumber = tesselation;
        int meridianNumber = tesselation * 2;
        constexpr float radius = 0.5f;

        std::vector<glm::vec3> vertices = {};
        const float dLambda = 2 * glm::pi<float>() / static_cast<float>(meridianNumber);
        const float dPhi = glm::pi<float>() / static_cast<float>(parallelNumber);
        unsigned int lastVertex = 0;

        for (int i = 0; i < parallelNumber; i++) {
            for (int j = 0; j < meridianNumber; j++) { // meridianNumber/2; ++j) {
                float lambda0 = static_cast<float>(j) * dLambda;
                float phi0 = static_cast<float>(i) * dPhi;
                float lambda1 = (j + 1) == meridianNumber ? 2 * glm::pi<float>() : static_cast<float>(j + 1) * dLambda;
                float phi1 = (i + 1) == parallelNumber ? glm::pi<float>() : static_cast<float>(i + 1) * dPhi;

                // Vertex order: 0, 1, 2, 1, 3, 2
                vertices.push_back(glm::vec3(glm::cos(lambda0) * glm::sin(phi0), glm::cos(phi0), glm::sin(lambda0) * glm::sin(phi0)) * radius);
                vertices.push_back(glm::vec3(glm::cos(lambda1) * glm::sin(phi0), glm::cos(phi0), glm::sin(lambda1) * glm::sin(phi0)) * radius);
                vertices.push_back(glm::vec3(glm::cos(lambda0) * glm::sin(phi1), glm::cos(phi1), glm::sin(lambda0) * glm::sin(phi1)) * radius);
                vertices.push_back(vertices[lastVertex + 1]);
                vertices.push_back(glm::vec3(glm::cos(lambda1) * glm::sin(phi1), glm::cos(phi1), glm::sin(lambda1) * glm::sin(phi1)) * radius);
                vertices.push_back(vertices[lastVertex + 2]);

                lastVertex += 6;
            }
        }

        return vertices;
    }

    static std::vector<glm::vec3> createCubeVec3() {
        return createVec3FromFloatList(cubeVertices, 108);
    }

    static constexpr glm::float32 cubeVertices[] = {
        -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f, 0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,
        0.5f, -0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        0.5f, -0.5f, -0.5f,
        0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        0.5f, 0.5f, 0.5f,
        0.5f, -0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, -0.5f,
        -0.5f, 0.5f, -0.5f,
        0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, -0.5f,
        -0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f,
        0.5f, -0.5f, 0.5f};

  private:
    /// Creates a vector of vec3 elements from a list of floats [x0, y0, z0, x1, y1, z1, x2, ..].
    /// @param vertices Pointer to the coordinate float array.
    /// @param length Single float elements in the array (3x the vertex count).
    static std::vector<glm::vec3> createVec3FromFloatList(const glm::float32 *vertices, size_t length) {
        assert((length % 3) == 0);

        std::vector<glm::vec3> out;
        size_t n = length / 3;
        out.resize(n);
        for (size_t i = 0; i < n; i++) {
            out[i].x = vertices[i * 3 + 0];
            out[i].y = vertices[i * 3 + 1];
            out[i].z = vertices[i * 3 + 2];
        }
        return out;
    }
};

} // namespace vvv