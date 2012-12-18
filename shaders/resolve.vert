attribute vec2 position;
varying vec2 texCoord;
void main() {
 gl_Position = vec4(position,0,1);
 texCoord = (position+1.f)/2.f;
}
