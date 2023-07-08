#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

// credits:
// https://www.shadertoy.com/view/XsX3zS

// ------ START SHADERTOY CODE -----
#define WAVES 8.0

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 uv = -1.0 + 2.0 * fragCoord.xy / iResolution.xy;

    float time = iTime * 1.0;

    vec3 color = vec3(0.0);

    for (float i=0.0; i<WAVES + 1.0; i++) {
        float freq = texture(iChannel0, vec2(i / WAVES, 0.0)).x * 7.0;

        vec2 p = vec2(uv);

        p.x += i * 0.04 + freq * 0.03;
        p.y += sin(p.x * 10.0 + time) * cos(p.x * 2.0) * freq * 0.2 * ((i + 1.0) / WAVES);
        float intensity = abs(0.01 / p.y) * clamp(freq, 0.35, 2.0);
        color += vec3(1.0 * intensity * (i / 5.0), 0.5 * intensity, 1.75 * intensity) * (3.0 / WAVES);
    }

    fragColor = vec4(color, 1.0);
}
// ------ END SHADERTOY CODE -----



void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    mainImage( fragColor, FlutterFragCoord().xy );
}



