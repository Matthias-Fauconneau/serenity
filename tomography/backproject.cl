struct mat4 { float4 columns[4]; };
kernel void backproject(const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, global const struct mat4* worldToView, read_only image3d_t images, sampler_t imageSampler, image3d_t Y) {
    size_t x = get_global_id(0); size_t y = get_global_id(1); size_t z = get_global_id(2);
    const float3 world = (float3)(x,y,z) - center;
    float Atb = 0;
    if(world.x*world.x + world.y*world.y <= radiusSq) {
        int2 imageSize = get_image_dim(images).xy;
        for(uint projectionIndex=0; projectionIndex<projectionCount; projectionIndex++) {
            struct mat4 M = worldToView[projectionIndex];
            float3 view = world.x * M.columns[0].xyz + world.y * M.columns[1].xyz + world.z * M.columns[2].xyz + M.columns[3].xyz; // Homogeneous view coordinates
            float2 image = view.xy / view.z + imageCenter; // Perspective divide + Image coordinates offset
            //if(image.x >= 0 && image.y >=0 && image.x <= imageSize.x-1 && image.y <= imageSize.y-1) {
              Atb += read_imagef(images, imageSampler, (float4)(image,projectionIndex,0)).x;
            //}
        }
    }
    // No projection and no regularization (x=0)
    write_imagef(Y, (int4)(x,y,z,0), Atb);
}
