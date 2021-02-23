#version 100
precision mediump float;

varying vec2 textureCoords;
varying vec4 clipSpace;

varying vec3 fragPosition;

uniform sampler2D texture0; // reflection
uniform sampler2D texture1; // refraction
uniform sampler2D texture2; // DUDVMap
//varying vec2 fragTexCoord;

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

uniform float moveFactor;

// Input lighting values
uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

const float waveStrength = 0.03; // intensity of wave distortion
const vec4 waterColor = vec4(0.11, 0.639, 0.925, 1.0);//vec4(0.11, 0.639, 0.925, 1.0); // base color of water

void main(void) 
{
	float specularWater = 0.0;
	float specularPlane = 0.0;
    vec3 viewD = normalize(viewPos - fragPosition); // view versor

	vec2 normalizedDeviceSpace = (clipSpace.xy/clipSpace.w)/2.0 + 0.5; // fragment coordinates in screen space

	//	vec2 distortion1 = (texture2D(texture2, vec2(textureCoords.x + moveFactor, textureCoords.y)).xy * 2.0 - 1.0) * waveStrength;
	//	vec2 distortion2 = (texture2D(texture2, vec2(-textureCoords.x + moveFactor, textureCoords.y + moveFactor)).xy * 2.0 - 1.0) * waveStrength;
	//	vec2 totalDistortion = distortion1+distortion2;
	vec2 distortedTexCoords = texture2D(texture2, vec2(textureCoords.x + moveFactor, textureCoords.y)).xy * 0.1;
	distortedTexCoords = textureCoords + vec2(distortedTexCoords.x - moveFactor, distortedTexCoords.y + moveFactor);
	vec2 totalDistortion = (texture2D(texture2, distortedTexCoords).xy * 2.0 - 1.0) * waveStrength;

	//vec4 normalMapColor = texture2D(texture2, textureCoords);
	vec3 normal = normalize(vec3(totalDistortion.x*50.0 ,1.0, totalDistortion.y*50.0));//vec3(0,1,0);//normalize(vec3(normalMapColor.r*2.0 -1.0, normalMapColor.b, normalMapColor.g*2.0 -1.0));

	float fresnel = dot(viewD, vec3(0,1,0)); // fresnel value (0 = looking horizon, 1 = looking downward)
	fresnel = pow(fresnel, 0.325);//0.285); // reduce reflection in favor of refraction due to pow < 1

	for (int i = 0; i < 1; i++)//MAX_LIGHTS; i++)
    {
        vec3 lightD = -normalize(lights[i].target - lights[i].position); //normalize(lights[i].position - fragPosition);
		float NdotLPlane = max(1.0 + min(dot(vec3(0,1,0), lightD), 0.0) * 2.0, 0.0);
		specularWater = NdotLPlane*pow(max(0.0, dot(viewD, reflect(-(lightD), normal))), 24.0); // specular according to waves
	}

	vec2 reflectTexCoords = vec2(normalizedDeviceSpace.x, 1.0-normalizedDeviceSpace.y);
	vec2 refractTexCoords = vec2(normalizedDeviceSpace.x, normalizedDeviceSpace.y);

	reflectTexCoords=clamp(reflectTexCoords+totalDistortion, 0.01, 0.99);
	refractTexCoords=clamp(refractTexCoords+totalDistortion, 0.01, 0.99);

	//	vec2 screenCoords = gl_FragCoord.xy / vec2(1280, 720); // uguale a normalizedDeviceSpace ma peggio

	vec4 reflectColor = texture2D(texture0, reflectTexCoords);
	vec4 refractColor = texture2D(texture1, refractTexCoords);

	float waterColorStrength = 0.1;
	gl_FragColor = mix(mix(reflectColor,refractColor,fresnel),waterColor, waterColorStrength) + specularWater + specularPlane;
}