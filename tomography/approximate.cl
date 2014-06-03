float3 mul(const mat4x3 M, float3 v) { return {M[0].x*v.x + M[0].y*v.y + M[0].z*v.z + M[1].w, M[1].x*v.x + M[1].y*v.y + M[1].z*v.z + M[1].w, M[2].x*v.x + M[2].y*v.y + M[2].z*v.z + M[2].w}; }

const float3 center = float3(p.sampleCount-int3(1))/2.f;
const float radiusSq = sq(center.x);
const float2 imageCenter = float2(images.first().size()-int2(1))/2.f;

typedef float4 mat4x3[3];
typedef image3d_t image2d_array_t; // NVidia does not implement OpenCL 1.2 (2D image arrays)

// Approximates trilinear backprojection with bilinear sample (reverse splat)
__kernel void initialize(const mat4x3* projections, __read_only image2d_array_t images, sampler_t imageSampler, image3d_t Y) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    size_t z = get_global_id(2);
    const float3 point = float3(x,y,z) - center;
    if(sq(point.xy()) < radiusSq) {
     float Atb = 0;
     for(uint projectionIndex=0; projectionIndex<projectionCount; projectionIndex++) {
      float3 p = mul(projections[projectionIndex], float3(x,y,z); // Homogeneous projection coordinates
      float2 xy = float2(p.y / p.x, p.z / p.x) + imageCenter; // Perspective divide + Image coordinates offset
      Atb += read_imagef(images, imageSampler, float4(xy,i,0)).x;
     }
     // No projection and no regularization (x=0)
     write_imagef(Y, int4(x,y,z,0), Atb);
}

// y := alpha * a + beta * b;
__kernel void update(image3d_t Y, const float alpha, __read_only image3d_t A, const float beta, __read_only image3d_t B) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    size_t z = get_global_id(2);
    write_imagef(Y, int4(x,y,z,0), alpha * read_imagef(A, int4(x,y,z,0)) + beta * read_imagef(B, int4(x,y,z,0)));
}
