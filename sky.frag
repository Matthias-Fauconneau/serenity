const vec3 sunColor = vec3(0.75, 0.5, 0.25);
const vec3 skyColor = vec3(0.25, 0.5, 0.75);
uniform vec3 sunLightDirection;

varying vec3 viewRay;

void main() {
 const float Kr=0.0025, Km=0.0001, g=-0.990;
 float cos = dot(sunLightDirection, normalize(viewRay));
 float miePhase = ((1.0 - g*g) / (2.0 + g*g)) * (1.0 + cos*cos) / pow(1.0 + g*g - 2.0*g*cos, 1.5);
 gl_FragColor.rgb = skyColor + sunColor*20.0*Km*miePhase;
}
