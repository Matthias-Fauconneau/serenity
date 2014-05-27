struct Projection {
 float3 origin;
 float halfHeightMinusOriginZ; // z/2 - origin.z
 float3 ray[3];
 float c; // origin.xy² - r²
};

__kernel void project(const struct Projection p, const float3 volumeSize, __read_only image3d_t volume, sampler_t volumeSampler, const uint width, __global float* image) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    const float3 ray = normalize(x * p.ray[0] + y * p.ray[1] + p.ray[2]);
    // Intersects cap disks
    const float2 capT = p.halfHeightMinusOriginZ / ray.z; // top bottom
    const float4 capXY = p.origin.xyxy + capT.xxyy * ray.xyxy; // topX topY bottomX bottomY
    const float4 capXY2 = capXY*capXY;
    const float2 capR2 = capXY2.s02 + capXY2.s13; // topR² bottomR²
    // Intersect cylinder side
    const float a = dot(ray.xy, ray.xy);
    const float b = 2*dot(p.origin.xy, ray.xy);
    const float sqrtDelta = sqrt(b*b - 4 * a * p.c);
    const float2 sideT = (-b + (float2)(sqrtDelta,-sqrtDelta)) / (2*a); // t±
    const float2 sideZ = fabs(p.origin.z + sideT * ray.z); // |z±|
    float tmin=INFINITY, tmax=-INFINITY;
    if(capR2.s0 < dot(volumeSize.xy,volumeSize.xy)) tmin=min(tmin, capT.s0), tmax=max(tmax, capT.s0); // top
    if(capR2.s1 < dot(volumeSize.xy,volumeSize.xy)) tmin=min(tmin, capT.s1), tmax=max(tmax, capT.s1); // bottom
    if(sideZ.s0 < volumeSize.z/2) tmin=min(tmin, sideT.s0), tmax=max(tmax, sideT.s0); // side+
    if(sideZ.s1 < volumeSize.z/2) tmin=min(tmin, sideT.s1), tmax=max(tmax, sideT.s1); // side-
    float3 position = p.origin + (volumeSize-1)/2 + tmin * ray; // [-size/2, size/2] -> [0, ]
    float accumulator = 0;
    while(tmin < tmax) { // Uniform ray sampling with trilinear interpolation
        accumulator += read_imagef(volume, volumeSampler, position.xyzz).x;
        tmin+=1; position += ray;
    }
    image[y*width+x] = accumulator;
}
