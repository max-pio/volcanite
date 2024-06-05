#ifndef DUMMY_ENVMAP_GLSL
#define DUMMY_ENVMAP_GLSL

vec2 pcg2d_random_dir(uvec2 v)
{
    // using pcg2d shader code from:
    // Jarzynski & Olano (2020) "Hash Functions for GPU Rendering"
    v *= 237u;
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v>>16u);
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^(v>>16u);
    // could use uniform sphere sampling instead, but this is good enough
    return normalize(vec2(v) - vec2(~0u / 2u));
}

float fade(float v) {
    return v * v * v * (v * (v * 6.f - 15.f) + 10.f);
}

float perlinNoise(vec2 uv, uint repeat_freq) {
    uvec2 cell = uvec2(uv);
    vec2 ab = fract(uv);

    const vec2 ofst[] = vec2[](vec2(0.f, 0.f), vec2(1.f, 0.f), vec2(0.f, 1.f), vec2(1.f, 1.f));
    float v[4];
    for(int i = 0; i < 4; i++)
    v[i] = dot(pcg2d_random_dir( (cell + uvec2(ofst[i])) % repeat_freq),
    ab - ofst[i]);

    return mix(mix(v[0], v[1], fade(ab.x)),
    mix(v[2], v[3], fade(ab.x)),
    fade(ab.y));
}

float fractralBrownianPerlin(vec2 uv, int octaves) {
    float v = 0.f;

    float amp = 2.f;
    uint freq = 2u;
    for(int o = 0; o < octaves; o++) {
        v += amp * perlinNoise(uv * float(freq), freq);

        amp *= 0.6f;
        freq *= 2u;
    }

    return 0.5f + 0.5f * v;
}


vec3 dummy_envmap(vec3 dir) {
    float cloud_density = clamp(dir.y * 3.f + 0.6f, 0.f, 1.f);

    // compute angles for noise lookup, normalized to [0, 1)
    float axz = atan(dir.z, dir.x) / (2.f * 3.1415f) + 0.5f;
    float ay = acos(dir.y) / (3.1415f);

    vec3 light_color = vec3(1.f, 0.6f, 0.f);
    vec3 base_color = vec3(0.4f, 0.6f, 1.f);

    vec3 c = vec3(0.f);
    c += mix(vec3(fractralBrownianPerlin(abs(vec2(axz, ay)), 8)), vec3(1.f), dir.y * dir.y * dir.y);
    c = mix(c, base_color, ay);
    c = mix(c, light_color, max(dir.x * dir.x * dir.x - 0.2f, 0.f));
    c += vec3(0.3f);

    return clamp(c, vec3(0.f), vec3(1.f));
}

#endif
