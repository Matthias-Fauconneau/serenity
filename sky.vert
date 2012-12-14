uniform mat4 inverseProjectionMatrix;

attribute vec2 position;

varying vec3 viewRay;

void main() {
 gl_Position = vec4(position,0.999,1);
 vec4 viewPos = (inverseProjectionMatrix * vec4(position.xy,1,1));
 viewRay = viewPos.xyz/viewPos.w;
}
