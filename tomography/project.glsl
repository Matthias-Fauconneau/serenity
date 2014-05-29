uniform mat3 rotation;
uniform vec3 origin;
uniform vec2 plusMinusHalfHeightMinusOriginZ; // ±z/2 - origin.z
uniform float c; // origin.xy² - r²
uniform float radiusSq;
uniform float halfHeight;
uniform vec3 dataOrigin; // origin + (volumeSize-1)/2
sampler3D volume;

varying vec3 rayDirection;
vertex {
 attribute vec4 position; // [-1, 1]
 gl_Position = position;
 rayDirection = rotation * position.xyz;
}
fragment {
    const vec3 ray = normalize(rayDirection);
    // Intersects cap disks
    const vec2 capT = plusMinusHalfHeightMinusOriginZ / ray.z; // top bottom
    const vec4 capXY = origin.xyxy + capT.xxyy * ray.xyxy; // topX topY bottomX bottomY
    const vec4 capXY2 = capXY*capXY;
    const vec2 capR2 = capXY2.s02 + capXY2.s13; // topR² bottomR²
    // Intersect cylinder side
    const float a = dot(ray.xy, ray.xy);
    const float b = 2*dot(origin.xy, ray.xy);
    const float sqrtDelta = sqrt(b*b - 4 * a * c);
    const vec2 sideT = (-b + vec2(sqrtDelta,-sqrtDelta)) / (2*a); // t±
    const vec2 sideZ = fabs(origin.z + sideT * ray.z); // |z±|
    const float infinity = 1. / 0.;
    float tmin=infinity, tmax=-infinity;
    if(capR2.s0 < radiusSq) tmin=min(tmin, capT.s0), tmax=max(tmax, capT.s0); // top
    if(capR2.s1 < radiusSq) tmin=min(tmin, capT.s1), tmax=max(tmax, capT.s1); // bottom
    if(sideZ.s0 < halfHeight) tmin=min(tmin, sideT.s0), tmax=max(tmax, sideT.s0); // side+
    if(sideZ.s1 < halfHeight) tmin=min(tmin, sideT.s1), tmax=max(tmax, sideT.s1); // side-
    vec3 position = dataOrigin + tmin * ray; // [-size/2, size/2[ -> [0, size[
    float accumulator = 0;
    while(tmin < tmax) { // Uniform ray sampling with trilinear interpolation
        accumulator += texture(volume, position).x;
        tmin+=1; position += ray;
    }
    out float output;
    output = accumulator;
}
