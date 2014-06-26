#define size_t uint
struct mat4 { float4 columns[4]; };
kernel void backproject(global float* Y, const uint XY, const uint X, const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, global const struct mat4* worldToDevice, read_only image3d_t images, sampler_t sampler) {
    size_t x = get_global_id(0); size_t y = get_global_id(1); size_t z = get_global_id(2);
    const float3 world = (float3)(x,y,z) - center;
    float Atb = 0;
    if(world.x*world.x + world.y*world.y <= radiusSq) {
        for(uint projectionIndex=0; projectionIndex<projectionCount; projectionIndex++) {
            struct mat4 M = worldToDevice[projectionIndex];
            float4 view = world.x * M.columns[0] + world.y * M.columns[1] + world.z * M.columns[2] + M.columns[3]; // Homogeneous view coordinates
            float2 image = view.xy / view.w + imageCenter; // Perspective divide + Image coordinates offset
            Atb += read_imagef(images, sampler, (float4)(image,projectionIndex+1./2,0)).x;
        }
    }
    Y[z*XY+y*X+x] = Atb;
}
