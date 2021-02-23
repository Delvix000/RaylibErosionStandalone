#version 100
precision mediump float;

// Input vertex attributes (from vertex shader)
varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
//varying vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0; // texture

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
uniform vec4 ambient;

void main()
{
    vec4 texelColor = texture2D(texture0, fragTexCoord);
    if (texelColor.a < 0.9) discard;
    // calculate tree color blend based on terrain normal
	vec3 light = vec3(0);
	light = -normalize(lights[0].target - lights[0].position);

	float NdotL = max(dot(fragColor.xyz * 2.0 - 1.0, light), 0.0);
    vec4 finalColor = mix(texelColor, ambient, mix(1.0 - ambient.a, 0.0, NdotL));
    finalColor.a = 1.0;
    gl_FragColor = finalColor;
}