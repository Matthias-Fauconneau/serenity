attribute vec3 position;
varying vec2 texCoord;
void main() {
 gl_Position = vec4(position,1);
 texCoord = (position.xy+1.f)/2.f;
}
