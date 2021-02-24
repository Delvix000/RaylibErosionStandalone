#version 100
precision mediump float;

// Input vertex attributes (from vertex shader)
varying vec3 fragPosition;

// Input uniform values
uniform sampler2D texture0;
uniform samplerCube environmentMapNight;
uniform samplerCube environmentMapDay;

uniform float daytime; // -1 = midnight, 0 = sunrise/sunset, 1 = midday
uniform float dayrotation; // same as daytime but is linear in the range 0-1
uniform float moveFactor; // for daytime cloud animation

#define     MAX_LIGHTS              1
#define     LIGHT_DIRECTIONAL       0
#define     LIGHT_POINT             1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

// Input lighting values
uniform Light lights[MAX_LIGHTS];
//uniform vec4 ambient;

// colorization functions
vec3 toGrayscale(in vec3 color)
{
  float average = dot(color.rgb, vec3(0.299, 0.587, 0.114));//(color.r + color.g + color.b) / 3.0;
  return vec3(average);
}
vec3 colorize(in vec3 grayscale, in vec3 color)
{
    return (grayscale * color);
}

mat3 rotAxis(vec3 axis, float a) 
{
    float s=sin(a);
    float c=cos(a);
    float oc=1.0-c;
    vec3 as=axis*s;
    mat3 p=mat3(axis.x*axis,axis.y*axis,axis.z*axis);
    mat3 q=mat3(c,-as.z,as.y,as.z,c,-as.x,-as.y,as.x,c);
    return p*oc+q;
}

void main()
{
    vec4 finalColor;
    vec3 nFragPosition = normalize(fragPosition);
    vec3 sunDir = normalize(lights[0].position);
    mat3 rot;
    if (daytime<0.0)
        rot = rotAxis(normalize(vec3(1,1,-1)), -(dayrotation + float(dayrotation<0.5))*3.14159265); // rotate stars around "earth axis"
    else
        rot = rotAxis(vec3(0,1,0), moveFactor); // rotate around zenith to move clouds

    vec4 texelColorNight = textureCube(environmentMapNight, -rot*fragPosition); // color from cubemap night
    vec4 texelColorDay = textureCube(environmentMapDay, -rot*fragPosition); // color from cubemap day
    
    float sunDot = clamp(dot(nFragPosition, sunDir),0.0,1.0);
    float sunStrength = pow(sunDot, 32.0); // sun spot in the sky (intensity relative to fragment)
    //vec4 sunColor = vec4(1.0, 1.0, 0.0, 1.0);
        
    float gradientHeight = clamp(nFragPosition.y*4.6, 0.0, 1.0); // 4.6 is sky gradient height. higher value -> shorter gradient
    // a sample from first horizontal pixel
    vec4 colorTop = mix(texture2D( texture0, vec2(daytime, 0.25)), texelColorNight, clamp(-daytime*1.5, 0.0, 1.0)); // lerp with night
    // a sample from second horizontal pixel
    vec4 colorBot = mix(mix(texture2D( texture0, vec2(daytime, 0.75)), vec4(0.0, 0.5, 1.0, 1.0), clamp(-daytime*0.8, 0.0, 1.0)), texelColorNight, clamp(-daytime, 0.0, 0.65)); // bot color kinda persists, and is bluer at night

    finalColor=mix(mix(colorBot, colorTop, gradientHeight), vec4(texelColorDay.rgb,1.0), pow(clamp(daytime, 0.0, 1.0), 0.75)); // base color is night/sunset/sunrise lerped with day
    finalColor += vec4(sunStrength); // add sunlight
    gl_FragColor = finalColor;
}

