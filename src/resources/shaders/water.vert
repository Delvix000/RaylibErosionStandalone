#version 100

varying vec2 textureCoords;
varying vec4 clipSpace;

// Input vertex attributes
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;

const float tiling = 1200.0; // size of repeated texture (higher -> smaller texture)

// Output vertex attributes (to fragment shader)
varying vec3 fragPosition;

void main()
{
    // Send vertex attributes to fragment shader
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    textureCoords = vertexTexCoord * tiling;
    clipSpace = mvp*vec4(vertexPosition, 1.0);

    // Calculate final vertex position
    gl_Position = clipSpace;
}