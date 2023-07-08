#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

// credits:
// https://www.shadertoy.com/view/Mlj3WV

// ------ START SHADERTOY CODE -----

/*
2D LED Spectrum - Visualiser
Based on Led Spectrum Analyser by: simesgreen - 27th February, 2013 https://www.shadertoy.com/view/Msl3zr
2D LED Spectrum by: uNiversal - 27th May, 2015
Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
*/

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // create pixel coordinates
    vec2 uv = fragCoord.xy / iResolution.xy;
    uv = vec2(uv.x, 1.0-uv.y);

    // quantize coordinates
    const float bands = 30.0;
    const float segs = 40.0;
    vec2 p;
    p.x = floor(uv.x*bands)/bands;
    p.y = floor(uv.y*segs)/segs;

    // read frequency data from first row of texture
    float fft  = texture( iChannel0, vec2(p.x,0.25) ).x;

    // led color
    vec3 color = mix(vec3(0.0, 2.0, 0.0), vec3(2.0, 0.0, 0.0), sqrt(uv.y));

    // mask for bar graph
    float mask = (p.y < fft) ? 1.0 : 0.1;

    // led shape
    vec2 d = fract((uv - p) *vec2(bands, segs)) - 0.5;
    float led = smoothstep(0.5, 0.35, abs(d.x)) *
    smoothstep(0.5, 0.35, abs(d.y));
    vec3 ledColor = led*color*mask;

    // output final color
    fragColor = vec4(ledColor, 1.0);
}
// ------ END SHADERTOY CODE -----



void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    // in this frag iTime is not used and on Android the SPIRV compiler remove it, so
    // when passing it with setFloat() it gives error
    mainImage( fragColor, FlutterFragCoord().xy  - iTime/9999999.);
}



