const vec3 skyColor = vec3(0.125, 0.25, 0.5);
const vec3 sunColor = vec3(0.875, 0.75, 0.5);

uniform vec3 sunLightDirection;
uniform vec3 skyLightDirection;

varying vec3 _sunLightPosition;
varying vec3 _color;
varying vec3 _normal;

/// Directional light with angular diameter
/*float angularLight(vec3 surfaceNormal, vec3 lightDirection, float angularDiameter) {
    float t = acos(dot(lightDirection,surfaceNormal)); // angle between surface normal and light principal direction
    float a = min<float>(PI/2,max(-PI/2,t-angularDiameter/2)); // lower bound of the light integral
    float b = min<float>(PI/2,max(-PI/2,t+angularDiameter/2)); // upper bound of the light integral
    float R = sin(b) - sin(a); // evaluate integral on [a,b] of cos(t-dt)dt (lambert reflectance model)
    R /= 2*sin(angularDiameter/2); // normalize
    return R;
}*/
/// For an hemispheric light, the integral bounds are always [t-PI/2,PI/2], thus R evaluates to (1-cos(t))/2
float hemisphericLight(vec3 surfaceNormal, vec3 lightDirection) { return (1.f+dot(lightDirection,surfaceNormal))/2.f; }
/// This is the limit of angularLight when angularDiameter → 0
float directionnalLight(vec3 surfaceNormal, vec3 lightDirection) { return max(0,dot(lightDirection,surfaceNormal)); }

void main() {
 vec3 normal = normalize(_normal);
 float sunLight = 1.f; //#TODO: soft shadows
 vec3 diffuseLight = hemisphericLight(normal,skyLightDirection)*skyColor + sunLight*directionnalLight(normal,sunLightDirection)*sunColor;
 vec3 diffuse = _color*diffuseLight;
 gl_FragColor = vec4(diffuse,1);
}
