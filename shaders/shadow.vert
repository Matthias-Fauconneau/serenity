uniform mat4 modelViewProjectionTransform;

attribute vec3 position;

void main() {
 gl_Position = modelViewProjectionTransform*vec4(position,1);
}
