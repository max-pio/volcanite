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

#ifndef DEBUG_COLORMAPS_H
#define DEBUG_COLORMAPS_H


// A collection of colormap approximations that you can use in your GLSL shaders.
// Use this only for testing out new stuff / prototyping / debugging since the computations are quite slow.
// Generally, you should use a texture to lookup color map or transfer function values.

vec3 colormap_blackwhite(float t) {
    t = clamp(t, 0.f, 1.f);
    return vec3(t);
}

// taken from CC0 https://www.shadertoy.com/view/WlfXRN
// fitting polynomials to matplotlib colormaps
//
// License CC0 (public domain)
//   https://creativecommons.org/share-your-work/public-domain/cc0/
//
//  - use degree 6 instead of degree 5 polynomials
//  - use nested horner representation for polynomials
//  - polynomials were fitted to minimize maximum error (as opposed to least squares)
//
// data fitted from https://github.com/BIDS/colormap/blob/master/colormaps.py
// (which is licensed CC0)
vec3 colormap_viridis(float t) {
    t = clamp(t, 0.f, 1.f);
    const vec3 c0 = vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
    const vec3 c1 = vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
    const vec3 c2 = vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
    const vec3 c3 = vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
    const vec3 c4 = vec3(6.228269936347081, 14.17993336680509, 56.69055260068105);
    const vec3 c5 = vec3(4.776384997670288, -13.74514537774601, -65.35303263337234);
    const vec3 c6 = vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}

vec3 colormap_inferno(float t) {
    t = clamp(t, 0.f, 1.f);
    const vec3 c0 = vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    const vec3 c1 = vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    const vec3 c2 = vec3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    const vec3 c3 = vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    const vec3 c4 = vec3(77.162935699427, -33.40235894210092, -81.80730925738993);
    const vec3 c5 = vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    const vec3 c6 = vec3(25.13112622477341, -12.24266895238567, -23.07032500287172);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}

vec3 colormap_plasma(float t) {
    t = clamp(t, 0.f, 1.f);
    const vec3 c0 = vec3(0.05873234392399702, 0.02333670892565664, 0.5433401826748754);
    const vec3 c1 = vec3(2.176514634195958, 0.2383834171260182, 0.7539604599784036);
    const vec3 c2 = vec3(-2.689460476458034, -7.455851135738909, 3.110799939717086);
    const vec3 c3 = vec3(6.130348345893603, 42.3461881477227, -28.51885465332158);
    const vec3 c4 = vec3(-11.10743619062271, -82.66631109428045, 60.13984767418263);
    const vec3 c5 = vec3(10.02306557647065, 71.41361770095349, -54.07218655560067);
    const vec3 c6 = vec3(-3.658713842777788, -22.93153465461149, 18.19190778539828);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}

// Copyright 2019 Google LLC.
// SPDX-License-Identifier: Apache-2.0
// Polynomial approximation in GLSL for the Turbo colormap
// Original LUT: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
// Authors:
//   Colormap Design: Anton Mikhailov (mikhailov@google.com)
//   GLSL Approximation: Ruofei Du (ruofei@google.com)
vec3 colormap_turbo(float t) {
    t = clamp(t, 0.f, 1.f);
    const vec4 kRedVec4 = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const vec4 kGreenVec4 = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const vec4 kBlueVec4 = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const vec2 kRedVec2 = vec2(-152.94239396, 59.28637943);
    const vec2 kGreenVec2 = vec2(4.27729857, 2.82956604);
    const vec2 kBlueVec2 = vec2(-89.90310912, 27.34824973);

    t = clamp(t,0.0,1.0);
    vec4 v4 = vec4( 1.0, t, t * t, t * t * t);
    vec2 v2 = v4.zw * v4.z;
    return vec3(
    dot(v4, kRedVec4)   + dot(v2, kRedVec2),
    dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
    dot(v4, kBlueVec4)  + dot(v2, kBlueVec2)
    );
}

// adapted color map IDL/CB-YIGnBu from https://github.com/kbinani/colormap-shaders
vec3 colormap_greenblue(float x) {
    float r;
    if (x < 0.2523055374622345) {
        r = (-5.80630393656902E+02 * x - 8.20261301968494E+01) * x + 2.53829637096771E+02;
    } else if (x < 0.6267540156841278) {
        r = (((-4.07958939010649E+03 * x + 8.13296992114899E+03) * x - 5.30725139102868E+03) * x + 8.58474724851723E+02) * x + 2.03329669375107E+02;
    } else if (x < 0.8763731146612115) {
        r = 3.28717357910916E+01 * x + 8.82117255504255E+00;
    } else {
        r = -2.29186583577707E+02 * x + 2.38482038123159E+02;
    }

    float g;
    if (x < 0.4578040540218353) {
        g = ((4.49001704856054E+02 * x - 5.56217473429394E+02) * x + 2.09812296466262E+01) * x + 2.52987561849833E+02;
    } else {
        g = ((1.28031059709139E+03 * x - 2.71007279113343E+03) * x + 1.52699334501816E+03) * x - 6.48190622715140E+01;
    }

    float b;
    if (x < 0.1239372193813324) {
        b = (1.10092779856059E+02 * x - 3.41564374557536E+02) * x + 2.17553885630496E+02;
    } else if (x < 0.7535201013088226) {
        b = ((((3.86204601547122E+03 * x - 8.79126469446648E+03) * x + 6.80922226393264E+03) * x - 2.24007302003438E+03) * x + 3.51344388740066E+02) * x + 1.56774650431396E+02;
    } else {
        b = (((((-7.46693234167480E+06 * x + 3.93327773566702E+07) * x - 8.61050867447971E+07) * x + 1.00269040461745E+08) * x - 6.55080846112976E+07) * x + 2.27664953009389E+07) * x - 3.28811994253461E+06;
    }

    return clamp(vec3(r, g, b) / 255.f, vec3(0.f), vec3(1.f));
}

// adapted color map IDL/CB-RdBu from https://github.com/kbinani/colormap-shaders
vec3 colormap_bluered(float t) {
    t = 1.f - clamp(t, 0.f, 1.f);
    float r, g, b;

    if (t < 0.09771832105856419) {
        r = 7.60263247863246E+02 * t + 1.02931623931624E+02;
    } else if (t < 0.3017162107441106) {
        r = (-2.54380938558548E+02 * t + 4.29911571188803E+02) * t + 1.37642085716717E+02;
    } else if (t < 0.4014205790737471) {
        r =  8.67103448276151E+01 * t + 2.18034482758611E+02;
    } else if (t < 0.5019932233215039) {
        r = -6.15461538461498E+01 * t + 2.77547692307680E+02;
    } else if (t < 0.5969483882550937) {
        r = -3.77588522588624E+02 * t + 4.36198819698878E+02;
    } else if (t < 0.8046060096654594) {
        r = (-6.51345897546620E+02 * t + 2.09780968434337E+02) * t + 3.17674951640855E+02;
    } else {
        r = -3.08431855203590E+02 * t + 3.12956742081421E+02;
    }

    if (t < 0.09881640500975222) {
        g = 2.41408547008547E+02 * t + 3.50427350427364E-01;
    } else if (t < 0.5000816285610199) {
        g = ((((1.98531871433258E+04 * t - 2.64108262469187E+04) * t + 1.10991785969817E+04) * t - 1.92958444776211E+03) * t + 8.39569642882186E+02) * t - 4.82944517518776E+01;
    } else if (t < 0.8922355473041534) {
        g = (((6.16712686949223E+03 * t - 1.59084026055125E+04) * t + 1.45172137257997E+04) * t - 5.80944127411621E+03) * t + 1.12477959061948E+03;
    } else {
        g = -5.28313797313699E+02 * t + 5.78459299959206E+02;
    }

    if (t < 0.1033699568661857) {
        b = 1.30256410256410E+02 * t + 3.08518518518519E+01;
    } else if (t < 0.2037526071071625) {
        b = 3.38458128078815E+02 * t + 9.33004926108412E+00;
    } else if (t < 0.2973267734050751) {
        b = (-1.06345054944861E+02 * t + 5.93327252747168E+02) * t - 3.81852747252658E+01;
    } else if (t < 0.4029109179973602) {
        b = 6.68959706959723E+02 * t - 7.00740740740798E+01;
    } else if (t < 0.5006715489526758) {
        b = 4.87348695652202E+02 * t + 3.09898550724286E+00;
    } else if (t < 0.6004396902588283) {
        b = -6.85799999999829E+01 * t + 2.81436666666663E+02;
    } else if (t < 0.702576607465744) {
        b = -1.81331701891043E+02 * t + 3.49137263626287E+02;
    } else if (t < 0.9010407030582428) {
        b = (2.06124143164576E+02 * t - 5.78166906665595E+02) * t + 5.26198653917172E+02;
    } else {
        b = -7.36990769230737E+02 * t + 8.36652307692262E+02;
    }

	r = clamp(r / 255.0, 0.0, 1.0);
	g = clamp(g / 255.0, 0.0, 1.0);
	b = clamp(b / 255.0, 0.0, 1.0);
	return vec3(r, g, b);
}

#endif // DEBUG_COLORMAPS_H
