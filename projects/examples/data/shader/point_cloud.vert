#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 instancePos;

layout (location = 0) out vec3 position_out;
layout (location = 1) out vec3 normal_out;
layout (location = 2) out vec3 campos_out;

layout (std140, set = 0, binding = 0) uniform per_frame_constants {
    mat4 world_to_projection_space;
    vec3 camera_pos;
};

void main()
{
    float pointSize = 0.2f;

    vec3 position = inPos * pointSize + instancePos;
    gl_Position = world_to_projection_space * vec4(position, 1);

    position_out = position;
    normal_out = normalize(inPos);
    campos_out = camera_pos;
}