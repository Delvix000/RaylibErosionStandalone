#version 100
precision mediump float;

// https://github.com/wwwtyro/glsl-atmosphere

// to check
// https://github.com/korgan00/TFG-Atmospheric-Scattering/tree/master/OGL-SDL_Template/app/shaders
// https://www.shadertoy.com/view/3dBSDW
// https://github.com/kosua20/opengl-skydome

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;

// Input uniform values
uniform sampler2D texture0;
uniform samplerCube environmentMap;

const float daytime = 1.0;

// Output fragment color
// out vec4 finalColor;

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

// Rayleigh & Mie scattering
// super expensive, keep low
#define PI 3.141592
#define iSteps 3
#define jSteps 2

vec2 rsi(vec3 r0, vec3 rd, float sr) {
    // ray-sphere intersection that assumes
    // the sphere is centered at the origin.
    // No intersection when result.x > result.y
    float a = dot(rd, rd);
    float b = 2.0 * dot(rd, r0);
    float c = dot(r0, r0) - (sr * sr);
    float d = (b*b) - 4.0*a*c;
    if (d < 0.0) return vec2(1e5,-1e5);
    return vec2(
        (-b - sqrt(d))/(2.0*a),
        (-b + sqrt(d))/(2.0*a)
    );
}

vec3 atmosphere(vec3 r, vec3 r0, vec3 pSun, float iSun, float rPlanet, float rAtmos, vec3 kRlh, float kMie, float shRlh, float shMie, float g) {
    // Normalize the sun and view directions.
    pSun = normalize(pSun);
    r = normalize(r);

    // Calculate the step size of the primary ray.
    vec2 p = rsi(r0, r, rAtmos);
    if (p.x > p.y) return vec3(0,0,0);
    p.y = min(p.y, rsi(r0, r, rPlanet).x);
    float iStepSize = (p.y - p.x) / float(iSteps);

    // Initialize the primary ray time.
    float iTime = 0.0;

    // Initialize accumulators for Rayleigh and Mie scattering.
    vec3 totalRlh = vec3(0,0,0);
    vec3 totalMie = vec3(0,0,0);

    // Initialize optical depth accumulators for the primary ray.
    float iOdRlh = 0.0;
    float iOdMie = 0.0;

    // Calculate the Rayleigh and Mie phases.
    float mu = dot(r, pSun);
    float mumu = mu * mu;
    float gg = g * g;
    float pRlh = 3.0 / (16.0 * PI) * (1.0 + mumu);
    float pMie = 3.0 / (8.0 * PI) * ((1.0 - gg) * (mumu + 1.0)) / (pow(1.0 + gg - 2.0 * mu * g, 1.5) * (2.0 + gg));

    // Sample the primary ray.
    for (int i = 0; i < iSteps; i++) {

        // Calculate the primary ray sample position.
        vec3 iPos = r0 + r * (iTime + iStepSize * 0.5);

        // Calculate the height of the sample.
        float iHeight = length(iPos) - rPlanet;

        // Calculate the optical depth of the Rayleigh and Mie scattering for this step.
        float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
        float odStepMie = exp(-iHeight / shMie) * iStepSize;

        // Accumulate optical depth.
        iOdRlh += odStepRlh;
        iOdMie += odStepMie;

        // Calculate the step size of the secondary ray.
        float jStepSize = rsi(iPos, pSun, rAtmos).y / float(jSteps);

        // Initialize the secondary ray time.
        float jTime = 0.0;

        // Initialize optical depth accumulators for the secondary ray.
        float jOdRlh = 0.0;
        float jOdMie = 0.0;

        // Sample the secondary ray.
        for (int j = 0; j < jSteps; j++) {

            // Calculate the secondary ray sample position.
            vec3 jPos = iPos + pSun * (jTime + jStepSize * 0.5);

            // Calculate the height of the sample.
            float jHeight = length(jPos) - rPlanet;

            // Accumulate the optical depth.
            jOdRlh += exp(-jHeight / shRlh) * jStepSize;
            jOdMie += exp(-jHeight / shMie) * jStepSize;

            // Increment the secondary ray time.
            jTime += jStepSize;
        }

        // Calculate attenuation.
        vec3 attn = exp(-(kMie * (iOdMie + jOdMie) + kRlh * (iOdRlh + jOdRlh)));

        // Accumulate scattering.
        totalRlh += odStepRlh * attn;
        totalMie += odStepMie * attn;

        // Increment the primary ray time.
        iTime += iStepSize;

    }

    // Calculate and return the final color.
    return iSun * (pRlh * kRlh * totalRlh + pMie * kMie * totalMie);
}

void main()
{
    // Fetch color from texture map
    vec3 color = textureCube(environmentMap, -fragPosition).rgb;
//
//    vec2 uvpos = vec2(0, 1.0 - fragPosition.y*0.5 + 0.5);
//    uvpos.x = 0.0;//a sample from first horizontal pixel
//    vec4 colour1 = texture2D( texture0, uvpos);
//    uvpos.x = 0.5;//a sample from second horizontal pixel
//    vec4 colour2 = texture2D( texture0, uvpos);
//
//    vec3 skyColor = vec3(1,0.5,0);//mix(colour1 , colour2, daytime);
//    //color+=skyColor.xyz;

    vec3 atmoColor = atmosphere(
            vec3(fragPosition.x, fragPosition.y+0.01, fragPosition.z),           // normalized ray direction
            vec3(0,6372e3,0),               // ray origin
            lights[0].position,                        // position of the sun
            66.0,                           // intensity of the sun
            6371e3,                         // radius of the planet in meters
            6421e3,                         // radius of the atmosphere in meters
            vec3(5.5e-6, 13.0e-6, 22.4e-6), // Rayleigh scattering coefficient
            21e-6,                          // Mie scattering coefficient
            8e3,                            // Rayleigh scale height
            1.2e3,                          // Mie scale height
            0.758                           // Mie preferred scattering direction
        );

    // Apply exposure.
    atmoColor = 1.0 - exp(-1.0 * atmoColor);

    // Convert to grayscale first:
    //vec3 grayscale = toGrayscale(color);

    // Then "colorize" by simply multiplying the grayscale
    // with the desired color.
    //color = colorize(grayscale, atmoColor);
    
    // Apply gamma correction
//    color = color/(color + vec3(1.0));
//    color = pow(color, vec3(1.0/2.2));

    gl_FragColor = vec4(color*0.1 + atmoColor, 1);

}

