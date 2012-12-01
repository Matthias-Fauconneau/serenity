uniform mat4 modelViewProjectionTransform;
uniform mat3 normalMatrix;
uniform mat4 sunLightTransform;

attribute vec3 position;
attribute vec3 color;
attribute vec3 normal;

varying vec3 _sunLightPosition;
varying vec3 _color;
varying vec3 _normal;

void main() {
 gl_Position = modelViewProjectionTransform*vec4(position,1);
 _sunLightPosition = (sunLightTransform*vec4(position,1)).xyz;
 _color = color;
 _normal = normalMatrix*normal;
}
