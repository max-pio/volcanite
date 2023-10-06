#ifndef SDF_H
#define SDF_H

// enlarges the object by h while simultaniously rounding corners
float sdf_opRound(in float d, in float h)
{
    return d - h;
}

// Primitive combinations https://www.iquilezles.org/www/articles/distfunctions/distfunctions.htm
float sdf_opUnion(float d1, float d2) { return min(d1, d2); }

float sdf_opSubtraction(float d1, float d2) { return max(-d1, d2); }

float sdf_opIntersection(float d1, float d2) { return max(d1, d2); }

float sdf_opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5*(d2-d1)/k, 0.0, 1.0);
    return mix(d2, d1, h) - k*h*(1.0-h); }

float sdf_opSmoothSubtraction(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5*(d2+d1)/k, 0.0, 1.0);
    return mix(d2, -d1, h) + k*h*(1.0-h); }

float sdf_opSmoothIntersection(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5*(d2-d1)/k, 0.0, 1.0);
    return mix(d2, d1, h) + k*h*(1.0-h); }

#endif /* SDF_H */