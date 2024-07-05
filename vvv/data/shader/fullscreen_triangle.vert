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

#version 450

vec2 positions[3] = vec2[](
vec2(-1.f, -1.f),
vec2(-1.f,  3.f),
vec2( 3.f, -1.f)
);

vec2 uvs[3] = vec2[](
vec2(0.f, 0.f),
vec2(0.f, 2.f),
vec2(2.f, 0.f)
);

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    uv = uvs[gl_VertexIndex];
}