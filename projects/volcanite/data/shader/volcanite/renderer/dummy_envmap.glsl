#ifndef DUMMY_ENVMAP_GLSL
#define DUMMY_ENVMAP_GLSL

float randSin(vec2 c){
    return fract(sin(dot(c.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

float noise(vec2 p, float freq ){
    float unit = 1./freq;
    vec2 ij = floor(p * freq);
    vec2 xy = mod(p,unit)/unit;
    //xy = 3.*xy*xy-2.*xy*xy*xy;
    xy = .5*(1.-cos(3.14159265358979f*xy));
    float a = randSin((ij+vec2(0.,0.)));
    float b = randSin((ij+vec2(1.,0.)));
    float c = randSin((ij+vec2(0.,1.)));
    float d = randSin((ij+vec2(1.,1.)));
    float x1 = mix(a, b, xy.x);
    float x2 = mix(c, d, xy.x);
    return mix(x1, x2, xy.y);
}

float perlinNoise(vec2 p, int res){
    float persistance = .5;
    float n = 0.;
    float normK = 0.;
    float f = 4.;
    float amp = 1.;
    int iCount = 0;
    for (int i = 0; i<50; i++){
        n+=amp*noise(p, f);
        f*=2.;
        normK+=amp;
        amp*=persistance;
        if (iCount == res) break;
        iCount++;
    }
    float nf = n/normK;
    return nf*nf*nf*nf;
}

vec3 dummy_envmap(vec3 dir) {
    vec3 c = vec3(0.f);
    c += mix(vec3(0.f, 0.1f, 0.3f), vec3(1.f) + vec3(perlinNoise(vec2((dir.x + dir.z), dir.y), 3)), dir.y * 0.5f + 0.5f);
    c = mix(c, vec3(1.f, 0.6f, 0.f), max(dir.x * dir.x * dir.x - 0.2f, 0.f));
    c += vec3(0.3f);
    return clamp(c, vec3(0.f), vec3(1.f));
}

#endif
