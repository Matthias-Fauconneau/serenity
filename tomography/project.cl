struct mat4 { float4 columns[4]; };
kernel void project(struct mat4 imageToWorld, float2 plusMinusHalfHeightMinusOriginZ /*±z/2 - origin.z*/, float c /*origin.xy² - r²*/, float radiusSq, float halfHeight, float3 dataOrigin /*origin + (volumeSize-1)/2*/,
                                read_only image3d_t volume, sampler_t sampler, const uint width, global float* image) {
    size_t x = get_global_id(0), y = get_global_id(1);
    const float3 ray = normalize(x * imageToWorld.columns[0].xyz + y * imageToWorld.columns[1].xyz + imageToWorld.columns[2].xyz);
    const float3 origin = imageToWorld.columns[3].xyz;
    // Intersects cap disks
    const float2 capT = plusMinusHalfHeightMinusOriginZ / ray.z; // top bottom
    const float4 capXY = origin.xyxy + capT.xxyy * ray.xyxy; // topX topY bottomX bottomY
    const float4 capXY2 = capXY*capXY;
    const float2 capR2 = capXY2.s02 + capXY2.s13; // topR² bottomR²
    // Intersect cylinder side
    const float a = dot(ray.xy, ray.xy);
    const float b = 2*dot(origin.xy, ray.xy);
    const float sqrtDelta = sqrt(b*b - 4 * a * c);
    const float2 sideT = (-b + (float2)(sqrtDelta,-sqrtDelta)) / (2*a); // t±
    const float2 sideZ = fabs(origin.z + sideT * ray.z); // |z±|
    float tmin=INFINITY, tmax=-INFINITY;
    if(capR2.s0 < radiusSq) tmin=min(tmin, capT.s0), tmax=max(tmax, capT.s0); // top
    if(capR2.s1 < radiusSq) tmin=min(tmin, capT.s1), tmax=max(tmax, capT.s1); // bottom
    if(sideZ.s0 <= halfHeight) tmin=min(tmin, sideT.s0), tmax=max(tmax, sideT.s0); // side+
    if(sideZ.s1 <= halfHeight) tmin=min(tmin, sideT.s1), tmax=max(tmax, sideT.s1); // side-
    float3 position = dataOrigin + tmin * ray; // [-(size-1)/2, (size-1)/2] -> [0, size-1]
    float accumulator = 0;
    while(tmin < tmax) { // Uniform ray sampling with trilinear interpolation
        int3 size = get_image_dim(volume).xyz;
        //accumulator += position.x<0 || position.x>size.x-1 || position.y<0 || position.y>size.y-1 || position.z<0 || position.z>size.z-1;
        accumulator += read_imagef(volume, sampler, position.xyzz).x;
        tmin+=1; position += ray;
    }
    image[y*width+x] = accumulator;
}
