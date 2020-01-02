#version 120

// position
attribute vec4 aPosition;
attribute vec4 aTexCoordinate;

// texture transform and mvp matrix
uniform mat4 uMVPMatrix;
uniform mat4 uTextureTransform;

// texture coordinates
varying vec2 vTexCoordinate;

void main() {
    // texcoord needs to be manipulated by the transform given back from the system
    vTexCoordinate = (uTextureTransform * aTexCoordinate).xy;
    gl_Position =  uMVPMatrix * aPosition;
}