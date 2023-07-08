#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

// credits:
// https://www.shadertoy.com/view/ws2yRt

// ------ START SHADERTOY CODE -----
vec3 COL1 = vec3(0.6,0.0,0.0);
vec3 COL2 = vec3(0.8,0.0,0.9);

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy/iResolution.xy;
//    uv = vec2(uv.x, 1.0-uv.y);

    
    float grid = float(mod(floor(uv.x * 500.0),14.0) < 0.5);
    grid += float(mod(floor(uv.y * 200.0),11.0) < 0.5); 
    grid = float(grid>0.5);
    
    vec2 uvn = 2.0 * uv - 1.0;
      
    grid *= 1.0-clamp(0.0,1.0,pow(length(uvn),1.2));
     
    // aquire wave
    float wa = texture(iChannel0,vec2(uv.x,0.75)).x;
    
    //attenuate
    float i = pow(1.0-abs(uv.y-wa),20.0);
    vec3 col = vec3(0.0,0.4,0.0)*grid+ vec3(i) * mix(COL1,COL2,i);
    fragColor = vec4(col,0.7);
}
// ------ END SHADERTOY CODE -----



void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    // in this frag iTime is not used and on Android the SPIRV compiler remove it, so
    // when passing it with setFloat() it gives error
    mainImage( fragColor, FlutterFragCoord().xy + iTime/999999999.);
}



