//uniform mat4 view, light;
attribute vec4 position; varying vec2 pos;\n
void main() { gl_Position = position + offset; pos = position.xy; }
