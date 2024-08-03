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

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout (std140, set = 0, binding = 0)
uniform gradient {
    vec4 colorTopLeft;
    vec4 colorBottomRight;
};

void main() {
    float l = length(uv)/length(vec2(1.f));
    outColor = l * colorTopLeft + (1.f - l) * colorBottomRight;
}