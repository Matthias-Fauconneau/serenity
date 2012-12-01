uniform sampler2D framebuffer;

varying vec2 texCoord;

float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1.0/2.4)-0.055; else return 12.92*c; }

void main() {
 vec3 c = texture2D(framebuffer, texCoord).rgb;
 gl_FragColor = vec4(sRGB(c.r), sRGB(c.g), sRGB(c.b), 1.0);
}
