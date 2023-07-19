#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    // in this frag iTime is not used and on Android the SPIRV compiler 
    // remove it, so when passing it with setFloat() it gives error
    vec2 uv = FlutterFragCoord().xy / iResolution.xy + iTime/999999999.;

    float data = texture( iChannel0, vec2(1.0 - uv.y, 1.0 - uv.x) ).x;
    fragColor = vec4(data, data*data, 0., 1.);
}
