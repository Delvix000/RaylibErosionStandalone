#version 100
precision mediump float;

varying vec2 textureCoords;
varying vec4 clipSpace;

// Input vertex attributes (from vertex shader)
//varying vec2 fragTexCoord;
//varying vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
//uniform vec4 colDiffuse;
varying vec3 fragPosition;

uniform float moveFactor;
uniform float daytime; // -1 = midnight, 0 = sunrise/sunset, 1 = midday

void main()
{
    vec4 finalColor = texture2D(texture0, textureCoords + moveFactor);
    finalColor.a *= pow(1.0-abs(daytime),3.0);
    gl_FragColor = finalColor;
}