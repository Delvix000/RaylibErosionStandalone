#version 100
precision mediump float;

// Input vertex attributes (from vertex shader)
varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
//varying vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0; // terrain gradient texture
uniform sampler2D texture2; // heightmap

uniform vec4 colDiffuse;
uniform float cullHeight; // height of the clip plane
uniform int cullType; // type of culling (0 = cull above, 1 = cull below, 2 = no cull)

uniform sampler2D rockNormalMap;

#define     MAX_LIGHTS              1
#define     LIGHT_DIRECTIONAL       0
#define     LIGHT_POINT             1

struct MaterialProperty {
    vec3 color;
    int useSampler;
    sampler2D sampler;
};

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

struct Gradient {
    vec3 tangent;
    vec3 biTangent;
    vec3 normal;
};

// Input lighting values
uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

const float minHeight = -1.2;
const float maxHeight = 5.8; // sarebbe 6.8
const float GrassSlopeThreshold = 0.17; // maximum slope where grass grows (higher = more grass)
const float GrassBlendAmount = 0.55; // how much grass blends with rock (higher = smoother gradient)

uniform float daytime; // -1 = midnight, 0 = sunrise/sunset, 1 = midday

Gradient CalculateGradient(sampler2D heightmap, float u, float v)
{
    // Value from trial & error.
    // Seems to work fine for the scales we are dealing with.
    float strength = 20.0;
    float ds = 1.0/512.0; // texture size

    float bl = abs(texture2D(heightmap, vec2(u-ds,  v+ds))).x;
    float b = abs(texture2D(heightmap,  vec2(u,     v+ds))).x;
    float br = abs(texture2D(heightmap, vec2(u+ds,  v+ds))).x;
    float l = abs(texture2D(heightmap,  vec2(u-ds,  v   ))).x;
    float r = abs(texture2D(heightmap,  vec2(u+ds,  v   ))).x;
    float tl = abs(texture2D(heightmap, vec2(u-ds,  v-ds))).x;
    float t = abs(texture2D(heightmap,  vec2(u,     v-ds))).x;
    float tr = abs(texture2D(heightmap, vec2(u+ds,  v-ds))).x;

    // Compute dx using Sobel:
    //           -1 0 1 
    //           -2 0 2
    //           -1 0 1
    float dX = tr + 2.0 * r + br - tl - 2.0 * l - bl;

    // Compute dy using Sobel:
    //           -1 -2 -1 
    //            0  0  0
    //            1  2  1
    float dY = bl + 2.0 * b + br - tl - 2.0 * t - tr;

    vec3 tangent = normalize(vec3 (1.0, dX * strength, 0.0));
    vec3 biTangent = normalize(vec3(0.0, -dY * strength, -1.0));
    vec3 normal = cross(tangent, biTangent);
    return Gradient(tangent, biTangent, normal); // better quality

    //return Gradient(vec3(0), vec3(0), normalize(vec3(-dX, 1.0 / strength, -dY))); // approximate
}

mat3 transpose(mat3 m)
{
  return mat3(m[0][0], m[1][0], m[2][0],
              m[0][1], m[1][1], m[2][1],
              m[0][2], m[1][2], m[2][2]);
}

void main()
{
    //if (int(fragPosition.y < cullHeight) == cullType) discard;
    if (cullType == 1 && (fragPosition.y < cullHeight)) discard;
    
    float normalizedHeight = clamp((fragPosition.y-minHeight)/(maxHeight-minHeight), 0.0, 1.0);
    vec4 grassColor = texture2D(texture0, vec2(normalizedHeight, 0.75));
    vec4 rockColor = texture2D(texture0, vec2(normalizedHeight, 0.25));
    vec3 lightDot = vec3(0.0);
    //vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    // calculate normal based on heightmap
    Gradient gradient = CalculateGradient(texture2, fragTexCoord.x, fragTexCoord.y);
    vec3 normal = gradient.normal;
    
    // shift normal based on normalmap
    if (cullType==2)
    {
        //Load normal from normal map
        vec4 normalMap = texture2D(rockNormalMap, fragTexCoord * 6.0); // 6 is normal map size
        //Change normal map range from [0, 1] to [-1, 1]
        float normalMapStrength = mix(0.2, 0.55, clamp((fragPosition.y-0.2)/0.35 ,0.0, 1.0)); // mix between world heights 0.2 and 0.55 (0.2+0.35) (which is approximately beach/mountain transition zone)
        normalMap = mix(vec4(0.0, 0.0, 1.0, 0.0), (2.0 * normalMap) - 1.0, normalMapStrength); 

        //Make sure tangent is completely orthogonal to normal
        vec3 tangent = normalize(vec3(1,0,0) - dot(vec3(1,0,0), normal)*normal);

        //Create the biTangent
        vec3 biTangent = cross(normal, tangent);

        //Create the "Texture Space"
        mat3 texSpace = transpose(mat3(tangent, biTangent, normal));

        //Convert normal from normal map to texture space
        normal = normalize(normalMap.xyz * texSpace);
    }

    // color based on normal
    float slope = 1.0-normal.y;
    float grassBlendHeight = GrassSlopeThreshold * (1.0-GrassBlendAmount);
    float grassWeight = 1.0-clamp((slope-grassBlendHeight)/(GrassSlopeThreshold-grassBlendHeight),0.0,1.0);
    vec4 texelColor = grassColor * grassWeight + rockColor * (1.0-grassWeight);


    vec4 finalColor = vec4(0.0);
    vec3 light = vec3(0.0);
    for (int i = 0; i < 1; i++)//MAX_LIGHTS; i++)
    {
	    light = -normalize(lights[i].target - lights[i].position);
        float NdotL = max(dot(normal, light), 0.0);
        lightDot += lights[i].color.rgb * NdotL;
        finalColor = mix(mix(vec4(ambient.rgb,1.0),texelColor,ambient.a),texelColor, NdotL);
    }
    
    // Gamma correction
    gl_FragColor = finalColor;//pow(finalColor, vec4(1.0/2.2));
    float foamW = mix(1.0,0.0,-clamp((daytime-0.4)*1.5,-1.0, 0.0)); // foam width decreases with night time
    if (cullType == 0 && (fragPosition.y > cullHeight-0.04*foamW && fragPosition.y < cullHeight+0.2*foamW))
        gl_FragColor = vec4(1.0);//mix(finalColor, vec4(1.0),  (fragPosition.y-cullHeight+0.1)/0.1);
}