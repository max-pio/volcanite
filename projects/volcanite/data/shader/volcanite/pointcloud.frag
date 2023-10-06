#version 450

// DEPRECATED

layout (location = 0) in vec3 world_position;

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

layout (location = 0) out vec4 outFragColor;

void main()
{
    // lambert reflectance
    vec2 normpos = gl_PointCoord * 2.f - vec2(1.f);
    float d2 = normpos.x * normpos.x + normpos.y * normpos.y;
    if (d2 > 1.f)
        discard;
    vec3 normal = vec3(normpos.x, -normpos.y, sqrt(1.f - d2));

    vec3 lightVec = world_position - phong_lightDirection.xyz;

    outFragColor.xyz = dot(phong_lightDirection.xyz, normal) * point_color.xyz / pow(dot(lightVec, lightVec), 2.f);
    outFragColor.a = 1.f;

    // gauss
//    float d2 = dot(gl_PointCoord * 2.f - vec2(1.f), gl_PointCoord * 2.f - vec2(1.f));
//    float gauss = exp(-d2/0.3f);
//    outFragColor = vec4(point_color.xyz * (0.5f + gauss * 0.5f), gauss);
//    if(outFragColor.a < 0.1f)
//        discard;
}