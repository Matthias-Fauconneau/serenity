uniform mat3 rotation;
uniform vec3 origin;
uniform vec2 plusMinusHalfHeightMinusOriginZ; // ±z/2 - origin.z
uniform float c; // origin.xy² - r²
uniform float radiusSq;
uniform float halfHeight;
uniform vec3 dataOrigin; // origin + (volumeSize-1)/2
uniform sampler3D volume;

varying vec3 rayDirection;
vertex {
 attribute vec4 position; // [-1, 1]
 gl_Position = position;
 rayDirection = rotation * position.xyz;
}
fragment {
    vec3 ray = normalize(rayDirection);
    // Intersects cap disks
    vec2 capT = plusMinusHalfHeightMinusOriginZ / ray.z; // top bottom
    vec4 capXY = origin.xyxy + capT.xxyy * ray.xyxy; // topX topY bottomX bottomY
    vec4 capXY2 = capXY*capXY;
    vec2 capR2 = capXY2.xz + capXY2.yw; // topR² bottomR²
    // Intersect cylinder side
    float a = dot(ray.xy, ray.xy);
    float b = 2*dot(origin.xy, ray.xy);
    float sqrtDelta = sqrt(b*b - 4 * a * c);
    vec2 sideT = (-b + vec2(sqrtDelta,-sqrtDelta)) / (2*a); // t±
    vec2 sideZ = abs(origin.z + sideT * ray.z); // |z±|
    float infinity = 1. / 0.;
    float tmin=infinity, tmax=-infinity;
    if(capR2.x < radiusSq) tmin=min(tmin, capT.x), tmax=max(tmax, capT.x); // top
    if(capR2.y < radiusSq) tmin=min(tmin, capT.y), tmax=max(tmax, capT.y); // bottom
    if(sideZ.x < halfHeight) tmin=min(tmin, sideT.x), tmax=max(tmax, sideT.x); // side+
    if(sideZ.y < halfHeight) tmin=min(tmin, sideT.y), tmax=max(tmax, sideT.y); // side-
    vec3 position = dataOrigin + tmin * ray; // [-size/2, size/2[ -> [0, size[
    float accumulator = 0;
    while(tmin < tmax) { // Uniform ray sampling with trilinear interpolation
        accumulator += texture(volume, position).x;
        tmin+=1; position += ray;
    }
    out float target;
    target = accumulator;
}
