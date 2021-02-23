#version 100

// Input vertex attributes
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;

uniform sampler2D sappo;
uniform float moveFactor;

// Output vertex attributes (to fragment shader)
varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
//varying vec3 fragNormal;

const float waveStrength = 0.067;

// NOTE: Add here your custom variables

mat3 inverse(mat3 m)
{
  float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2];
  float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2];
  float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2];

  float b01 = a22*a11 - a12*a21;
  float b11 = -a22*a10 + a12*a20;
  float b21 = a21*a10 - a11*a20;

  float det = a00*b01 + a01*b11 + a02*b21;

  return mat3(b01, (-a22*a01 + a02*a21), (a12*a01 - a02*a11),
              b11, (a22*a00 - a02*a20), (-a12*a00 + a02*a10),
              b21, (-a21*a00 + a01*a20), (a11*a00 - a01*a10))/det;
}

mat3 transpose(mat3 m)
{
  return mat3(m[0][0], m[1][0], m[2][0],
              m[0][1], m[1][1], m[2][1],
              m[0][2], m[1][2], m[2][2]);
}

void main()
{
    // Send vertex attributes to fragment shader

    vec2 animationOffset = (mvp*vec4(vertexPosition, 1.0)).xz * 320.0; // relative to view, looks better imho
    vec2 totalDistortion = vec2(cos(radians(moveFactor*360.0 + animationOffset.x)), sin(radians(moveFactor*720.0 + animationOffset.y))*0.3) * waveStrength * (1.0 - vertexTexCoord.y);//(texture2D(sappo, vec2(moveFactor)).xy -0.5) * 2.0;
    vec3 offset = vec3(totalDistortion.x, 0, totalDistortion.y);

    fragPosition = vec3(matModel*vec4(vertexPosition + offset, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

//    mat3 normalMatrix = transpose(inverse(mat3(matModel)));
//    fragNormal = normalize(normalMatrix*vertexNormal);
    
    //fragTexCoord = vertexTexCoord;

    // Calculate final vertex position
    gl_Position = mvp*vec4(vertexPosition + offset, 1.0);
}