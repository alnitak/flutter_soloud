#version 460 core
#include <flutter/runtime_effect.glsl>
precision mediump float;

uniform sampler2D iChannel0;
uniform vec2 uResolution;
uniform float iTime;

out vec4 fragColor;

vec3 iResolution;

// credits:
// https://www.shadertoy.com/view/Xd3GRX

// ------ START SHADERTOY CODE -----
/*
	If you have any ideas please let me know in the comments! :)
	Especially if you have a better idea for the frequency distribution.
	Right now it's MAXF/2^(x/2) where x is a frequency band numbered from 0 to 15.
	It would probably be smart to include more than a single frequency over each band.

*/
//Enable dithering
#define ENABLEDITHER

//Enable rotation
//#define ENABLEROT

//Enable zooming with beat (sucks ass)
//#define ENABLEBEAT

//Show only the back wall without perspective
//#define FLAT

//Enable waveform
#define ENABLEWAVE

//Waveform amplitude; adjust according to audio source volume
#define WAVEAMP 1.0

//Height for gradient middle color; set to 1.0 for 2 color gradient
#define GRADVAL 0.8

#define time iTime


const vec3 Color1 = vec3(0.25,0.625,0.625);
const vec3 Color2 = vec3(0.25,0.25,1.00); // TEAL->BLUE->RED
const vec3 Color3 = vec3(1.00,0.25,0.25);

//Some color combinations

/*const vec3 Color1 = vec3(0.25,1.00,0.25);
const vec3 Color2 = vec3(0.75,0.50,0.25); // GREEN->YELLOW->RED
const vec3 Color3 = vec3(1.00,0.25,0.25);*/

/*const vec3 Color1 = vec3(0.00,0.00,0.00);
const vec3 Color2 = vec3(0.70,0.55,0.25); // BLACK->YELLOW->WHITE
const vec3 Color3 = vec3(0.5);*/

/*const vec3 Color1 = vec3(0.25,0.25,1.00);
const vec3 Color2 = vec3(0.25,0.625,0.625); // BLUE->TEAL->WHITE
const vec3 Color3 = vec3(0.5);*/


/*const vec3 Color1 = vec3(0.75); // WHITE
//const vec3 Color1 = vec3(0.25,0.50,1.00); // LIGHT BLUE
const vec3 Color2 = Color1;
const vec3 Color3 = Color1;*/

float noise(vec2 p)
{
    return fract(sin(dot(p ,vec2(12.9898,78.233))) * 43758.5453);
}

float noise3D(vec3 p)
{
    return fract(sin(dot(p ,vec3(12.9898,78.233,128.852))) * 43758.5453);
}


mat3 rot(vec3 ang)
{
    mat3 x = mat3(1.0,0.0,0.0,0.0,cos(ang.x),-sin(ang.x),0.0,sin(ang.x),cos(ang.x));
    mat3 y = mat3(cos(ang.y),0.0,sin(ang.y),0.0,1.0,0.0,-sin(ang.y),0.0,cos(ang.y));
    mat3 z = mat3(cos(ang.z),-sin(ang.z),0.0,sin(ang.z),cos(ang.z),0.0,0.0,0.0,1.0);
    return x*y*z;
}

vec3 getCol(float v, vec3 col1, vec3 col2)
{
    v = clamp(v,0.0,1.0);
    vec3 res = vec3(0.0);
    for(int i = 0; i<3; i++)
    {
        res[i] = col1[i] + v * (col2[i] - col1[i]);
    }
    return res;
}

float udBox( vec3 p, vec3 b )
{
    return length(max(abs(p)-b,0.0));
}

vec4 map(vec3 rp)
{
    vec4 d = vec4(0.0);

    vec3 bgCol = vec3(0.125,0.125,0.125);

    #ifdef ENABLEWAVE
    float waveF = texture(iChannel0, vec2((rp.x+1.8)/3.6, 0.75)).r;
    float waveDist = udBox(rp-vec3(0.0,-waveF*WAVEAMP+WAVEAMP/2.0,0.25), vec3(1.8,0.02,0.001));
    vec4 wave = vec4(vec3(0.3), waveDist);
    #endif

    float backWallDist = udBox(rp-vec3(0.0,0.0,0.75), vec3(1.8, 1.1, 0.5));
    vec4 backWall = vec4(bgCol, backWallDist);

    float edgeLeftDist = udBox(rp-vec3(1.9,0.0,0.3725), vec3(0.1,1.1,1.0));
    vec4 edgeLeft = vec4(bgCol, edgeLeftDist);

    float edgeRightDist = udBox(rp-vec3(-1.9,0.0,0.3725), vec3(0.1,1.1,1.0));
    vec4 edgeRight = vec4(bgCol, edgeRightDist);

    float edgeTopDist = udBox(rp-vec3(0.0,1.2,0.3725), vec3(2.0,0.1,1.0));
    vec4 edgeTop = vec4(bgCol,edgeTopDist);

    float edgeBotDist = udBox(rp-vec3(0.0,-1.2,0.3725), vec3(2.0,0.1,1.0));
    vec4 edgeBot = vec4(bgCol,edgeBotDist);

    d = edgeLeft.a > backWall.a ? backWall : edgeLeft;
    d = edgeRight.a > d.a ? d : edgeRight;
    d = edgeTop.a > d.a ? d : edgeTop;
    d = edgeBot.a > d.a ? d : edgeBot;

    #ifdef ENABLEWAVE
    d = wave.a > d.a ? d : wave;
    #endif

    return d;

}

vec3 normal(vec3 rp)
{
    vec3 eps = vec3( 0.002 ,0.0,0.0);
    return normalize( vec3(
                          map(rp+eps.xyy).a - map(rp-eps.xyy).a,
                          map(rp+eps.yxy).a - map(rp-eps.yxy).a,
                          map(rp+eps.yyx).a - map(rp-eps.yyx).a ) );

}

float light(vec3 lp, vec3 rp, vec3 n, float pulse)
{
    return (1.5*(pulse+0.05)/pow(distance(lp,rp),2.0))*max(dot(normalize(lp-rp), n), 0.0)*0.25;
}

float drawLight(vec3 lp, vec3 rd, vec3 ro, float td, float pulse)
{
    float res = 0.0;
    if(td > distance(lp,ro))
    {
        float dlp = length(cross(lp-ro, lp-(ro+rd)))/length((ro+rd)-ro);
        res=max(exp(-dlp*64.0*((1.0-pulse)+0.5)),0.0);
    }
    return res;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    uv = vec2(uv.x, 1.0-uv.y);
    vec2 p = 2.0*uv-1.0;
    p.x*=iResolution.x/iResolution.y;
//    vec2 m = iMouse.xy/iResolution.xy;
//    m.x = iMouse.x == 0.0 ? 0.5 : iMouse.x/iResolution.x;
    vec2 m = vec2(0.5, 0.5);
    m = 2.0*m-1.0;
    vec3 col = vec3(0.00);

    float hArr[16];
    for(int i = 0; i < 16; i++)
    {
        hArr[i] = texture(iChannel0, vec2( 1.0/pow(2.0,float(i)*0.5)-0.001,0.25)).r;
    }

    float z = -0.5;

    #ifdef ENABLEROT
    z = -2.0*(sin(time*0.25)*0.5+0.5);
    #endif
    #ifdef ENABLEBEAT
    z += -2.0*hArr[14];
    #endif
    vec3 ro = vec3(0.0,0.0,z-2.0);
    #ifdef FLAT
    ro = vec3(p/vec2(1.0,1.25), z-2.0);
    #endif
    vec3 rd = normalize( vec3(p, z) - ro );

    vec3 ang = vec3( 0.0, 0.0 , 0.0);
    #ifdef ENABLEROT
    ang = vec3(1.54*sin(time*0.125)*0.25, 1.54*cos(time*0.25)*0.25, 0.0);
    #endif
    ro*=rot(ang);
    rd*=rot(ang);

    vec3 rp = vec3(0.0);

    vec4 d = vec4(0.0);
    float td = 0.5;
    float dMax = 25.0;

    for(int i = 0; i < 64; i++)
    {
        if(td >= dMax) break;
        rp = ro+rd*td;
        d = map(rp);
        if(d.a < 0.001)
        {
            break;
        }
        td += d.a*0.75;
    }

    vec3 lp[16];

    for(int i = 0; i < 16; i++)
    {
        lp[i] = vec3( (7.5-float(i))*0.2125, hArr[i]*2.0-1.0, 0.0);
    }

    float g = 1.0/(1.0-GRADVAL);
    float r = 1.0/GRADVAL;

    vec3 lc[16];

    for(int i = 0; i < 16; i++)
    {
        lc[i] = getCol((hArr[i]-GRADVAL)*g, getCol(hArr[i]*r, Color1, Color2), Color3);
    }

    vec3 n = normal(rp);
    if(d.a < 0.001)
    {

        vec3 illumination = vec3(0.01);

        for(int i = 0; i < 16; i++)
        {
            illumination += lc[i] * light(lp[i], rp, n, hArr[i] );
        }

        col = d.rgb*illumination;
    }

    for(int i = 0; i < 16; i++)
    {
        col += lc[i] * drawLight(lp[i], rd, ro, td, hArr[i] );
    }

    col = clamp(col, 0.0, 1.0);

    col = pow(col, vec3(0.45)); //gamma adjust

    float f = 8.0;
    col = (1.0/(1.0+exp(4.0-f*col))-0.0003)/(0.982-0.018); //contrast
    p.x/=iResolution.x/iResolution.y;
    col *= smoothstep( 1.325, 0.825, abs(p.x) ); //dark edges
    col *= smoothstep( 1.325, 0.825, abs(p.y) );

    #ifdef ENABLEDITHER
    float dither = 4.0/256.0;

    col += (noise3D(vec3(p,fract(time)))*2.0-1.0)*dither;
    #endif

    fragColor = vec4(col,1.0);
}
// ------ END SHADERTOY CODE -----



void main() {
    iResolution = vec3(uResolution.x, uResolution.y, 0.);

    mainImage( fragColor, FlutterFragCoord().xy );
}



