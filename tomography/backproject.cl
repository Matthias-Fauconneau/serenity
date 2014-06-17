#define size_t uint
struct mat4 { float4 columns[4]; };
kernel void backproject(global float* Y, const uint XY, const uint X, const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, global const struct mat4* worldToView, read_only image3d_t images, sampler_t sampler) {
    size_t x = get_global_id(0); size_t y = get_global_id(1); size_t z = get_global_id(2);
    const float3 world = (float3)(x,y,z) - center;
    float Atb = 0;
    if(world.x*world.x + world.y*world.y <= radiusSq) {
        for(uint projectionIndex=0; projectionIndex<projectionCount; projectionIndex++) {
            struct mat4 M = worldToView[projectionIndex];
            float3 view = world.x * M.columns[0].xyz + world.y * M.columns[1].xyz + world.z * M.columns[2].xyz + M.columns[3].xyz; // Homogeneous view coordinates
            float2 image = view.xy / view.z + imageCenter; // Perspective divide + Image coordinates offset
            Atb += read_imagef(images, sampler, (float4)(image,projectionIndex,0)).x;
        }
    }
    Y[z*XY+y*X+x] = Atb;
}
