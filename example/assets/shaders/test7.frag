#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

// credits:
// https://www.shadertoy.com/view/3dSyRK

// ------ START SHADERTOY CODE -----
#define PI 3.1415926
vec3 palette( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d ) {
    return a + b*cos( 6.28318*(c*t+d) );
}

mat3 rot(vec3 ang) {
    mat3 x = mat3(1.0,0.0,0.0,0.0,cos(ang.x),-sin(ang.x),0.0,sin(ang.x),cos(ang.x));
    mat3 y = mat3(cos(ang.y),0.0,sin(ang.y),0.0,1.0,0.0,-sin(ang.y),0.0,cos(ang.y));
    mat3 z = mat3(cos(ang.z),-sin(ang.z),0.0,sin(ang.z),cos(ang.z),0.0,0.0,0.0,1.0);
    return x*y*z;
}

float noise3D(vec3 p)
{
    return fract(sin(dot(p ,vec3(12.9898,78.233,128.852))) * 43758.5453)*2.0-1.0;
}

float loudness(float i, float mag) {
    return 10.0*mag*sqrt(log(i + 1.0));
}

float sphereDistance(vec3 st, vec3 center, float radius) {
    return (distance(st, center) - radius);
}

vec3 opRep( in vec3 p, in vec3 c) {
    vec3 q = mod(p,c)-0.5*c;
    return q;
}

vec3 map( in vec3 p ) {
    vec3 rep = vec3(118.0);
    vec3 randIndex = floor(p.xyz/rep.xyz);
    float rand = noise3D(randIndex);
    float f = 1.0*noise3D(1.13*floor(p.xyz/rep.xyz));
    float spectrumRange = abs(f)*0.40;
    float freqMag = 1.0*texture(iChannel0, vec2(spectrumRange,0.0)).x;
    freqMag = loudness(spectrumRange, freqMag);
    p = opRep( p, rep);
    vec4 sphere = vec4(rand*0.15*rep.x, f*0.15*rep.y, (0.5*f + 0.5*rand)*0.15*rep.z, 1.0 + 5.0*abs(f));
    float dist = sphereDistance(p, sphere.xyz, sphere.w);

    vec3 result = vec3(dist, f, freqMag);
    return result;
}

vec3 GetSphereIndexColor(float index) {
    vec3 cA = vec3(0.2, 0.6, 0.9);
    vec3 cB = vec3(0.3, 0.5, 0.2);
    vec3 cC = vec3(1.0, 1.0, 1.0);
    vec3 cD = vec3(0.1, 0.2, 0.5);
    return palette(index + 0.4, cA, cB, cC, cD);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 p = (2.*fragCoord - iResolution.xy ) / iResolution.y;
    // Camera setup.
    vec3 viewDir = vec3(0.0,0.0,1.0);
    vec3 camUp = vec3(0.0,1.0,0.0);
    vec3 camPos = vec3(0.0);
    vec3 u = normalize(cross(camUp,viewDir));
    vec3 v = cross(viewDir,u);
    vec3 vcv = (camPos + viewDir);
    vec3 srcCoord = vcv+p.x*u+p.y*v;
    vec3 rayDir = rot(vec3(0.3*iTime,sin(0.3*iTime),cos(0.223*iTime)))*normalize(srcCoord - camPos);

    vec4 c = vec4(0.0,0.0,0.0,1.0);

    float depth = 0.0;
    float d = 0.0;
    vec3 pos = vec3(0);
    vec3 colorAcc = vec3(0);
    for (int i = 0; i < 109; i++) {
        pos = camPos + rayDir * depth + 40.0*iTime;
        vec3 mapRes = map(pos);
        d = mapRes.x;
        float lightFalloffFactor = 1.2 - 0.6*mapRes.z;
        lightFalloffFactor = clamp(lightFalloffFactor, 0.1, 1.0);
        colorAcc += exp(-d*lightFalloffFactor) * 1.0*GetSphereIndexColor(mapRes.y);
        depth += d*(0.4);
    }
    c = vec4(colorAcc*0.24,1.0);

    fragColor = c;
}

// ------ END SHADERTOY CODE -----



void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    mainImage( fragColor, FlutterFragCoord().xy );
}
