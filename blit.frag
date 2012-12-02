//uniform vec4 color;
uniform sampler2D sampler;

varying vec2 _texCoord;

void main() {
 gl_FragColor = texture2D(sampler, _texCoord);
 //gl_FragColor = vec4(_texCoord,0,1);
}
