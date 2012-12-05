uniform vec4 color;
uniform sampler2D sampler;

varying vec2 _texCoord;

void main() {
 gl_FragColor = color * texture2D(sampler, _texCoord);
}
