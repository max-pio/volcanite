#version 450


// DEPRECATED

layout (location = 0) in vec3 position;

layout (std140, set = 0, binding = 0) uniform per_frame_constants {
    mat4 world_to_view_space;
    mat4 world_to_projection_space;
    mat4 projection_to_world_space;
    mat3 pixel_to_ray_direction_world_space;
    vec4 camera_position_world_space;
    vec4 phong_ambientLightColor;
    vec4 phong_diffuseLightColor;
    vec4 phong_specularLightColor;
    vec4 phong_lightDirection;
    float phong_specularExponent;
    ivec2 output_resolution;
};

layout (std140, set = 1, binding = 0) uniform nastja {
    float point_size;
    vec4 point_color;
    mat4 model_matrix;
    int frame;
};

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_PointSize;
};

layout (location = 0) out vec3 world_position_out;

void main()
{
    vec4 world_position = model_matrix * vec4(position, 1.f);
    world_position_out = world_position.xyz;

    gl_Position = world_to_projection_space * world_position;
    gl_PointSize = clamp(point_size/gl_Position.z, 1.f, 64.f);
}