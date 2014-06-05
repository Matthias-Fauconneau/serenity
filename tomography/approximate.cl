typedef float4 mat4x3[3];
inline float3 mul(const mat4x3 M, float3 v) { return (float3)(M[0].x*v.x + M[0].y*v.y + M[0].z*v.z + M[0].w, M[1].x*v.x + M[1].y*v.y + M[1].z*v.z + M[1].w, M[2].x*v.x + M[2].y*v.y + M[2].z*v.z + M[2].w); }
//typedef image3d_t image2d_array_t; // NVidia does not implement OpenCL 1.2 (2D image arrays)

// Approximates trilinear backprojection with bilinear sample (reverse splat)
kernel void backproject(const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const mat4x3* projections, read_only image3d_t images, sampler_t imageSampler, image3d_t Y) {
//kernel void backproject(const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const mat4x3* projections, read_only image3d_t images, sampler_t imageSampler, const size_t XY, const size_t X, global float* Y) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    size_t z = get_global_id(2);
    const float3 point = (float3)(x,y,z) - center;
    float Atb = 0;
    if(point.x*point.x + point.y*point.y < radiusSq) {
        for(uint projectionIndex=0; projectionIndex<projectionCount; projectionIndex++) {
            float3 p = mul(projections[projectionIndex], point); // Homogeneous projection coordinates
            //mat4x3 M = projections[projectionIndex];
            //float3 p = float3(M[0].x*x + M[0].y*y + M[0].z*z + M[1].w, M[1].x*x + M[1].y*y + M[1].z*z + M[1].w, M[2].x*x + M[2].y*y + M[2].z*z + M[2].w);
            if(p.x) {
             float2 xy = (float2)(p.y / p.x, p.z / p.x) + imageCenter; // Perspective divide + Image coordinates offset
             Atb += read_imagef(images, imageSampler, (float4)(xy.x,xy.y,z,0)).x;
            }
        }
    }
    // No projection and no regularization (x=0)
    //Y[z*XY+y*X+x] = Atb;
    write_imagef(Y, (int4)(x,y,z,0), Atb);
}

// y := alpha * a + beta * b;
kernel void update(image3d_t Y, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    size_t z = get_global_id(2);
    write_imagef(Y, (int4)(x,y,z,0), alpha * read_imagef(A, (int4)(x,y,z,0)) + beta * read_imagef(B, (int4)(x,y,z,0)));
}
