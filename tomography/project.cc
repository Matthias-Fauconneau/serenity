#include "project.h"
#include "thread.h"

#if CL
#include "opencl.h"

KERNEL(project)

struct CLProjection {
 float3 origin;
 float2 plusMinusHalfHeightMinusOriginZ; // halfHeight - origin.z
 float3 ray[3];
 float c; // origin.xy² - r²
 float radiusSq;
 float halfHeight;
 float3 dataOrigin; // origin + (volumeSize-1)/2
};

void project(const ImageF& image, const VolumeF& volume, const Projection& projection) {
    static cl_kernel kernel = project();

    float radius = float(volume.sampleCount.x-1)/2;
    float halfHeight = float(volume.sampleCount.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
    float3 dataOrigin = {float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1 -1 /*FIXME*/)/2};
    CLProjection clProjection = {projection.glOrigin, (float2){halfHeight,-halfHeight} - projection.glOrigin.z, {projection.glRotation[0],projection.glRotation[1],projection.glRotation[2]}, sq(projection.glOrigin.xy()) - radius*radius, radius*radius, halfHeight, (float3){projection.glOrigin.x,projection.glOrigin.y,projection.glOrigin.z} + dataOrigin};
    clSetKernelArg(kernel, 0, sizeof(CLProjection), &clProjection); //TODO: generic argument wrapper

    cl_image_format format = {CL_R, CL_FLOAT};
#if CL_1_2
    cl_image_desc desc = {CL_MEM_OBJECT_IMAGE3D, (size_t)volume.sampleCount.x, (size_t)volume.sampleCount.y, (size_t)volume.sampleCount.z, 0,0,0,0,0,0};
    cl_mem clSource = clCreateImage(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, &format, &desc, volume.data, 0);
#else
    static cl_mem clSource = clCreateImage3D(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, &format, volume.sampleCount.x, volume.sampleCount.y, volume.sampleCount.z, 0,0, volume.data, 0);
#endif
    void* volumeMap = clEnqueueMapBuffer(queue, clSource, true, CL_MAP_READ, 0, volume.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 1, sizeof(clSource), &clSource);

    cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    clSetKernelArg(kernel, 2, sizeof(sampler), &sampler);

    clSetKernelArg(kernel, 3, sizeof(image.width), &image.width);

    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY|CL_MEM_USE_HOST_PTR, image.data.size*sizeof(float), (void*)image.data.data, 0);
    void* imageMap = clEnqueueMapBuffer(queue, clImage, true, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 4, sizeof(clImage), &clImage);

    size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0, 0, 0, 0) );

    clEnqueueUnmapMemObject(queue, clImage, imageMap, 0, 0, 0);
    clReleaseMemObject(clImage);
    //clEnqueueUnmapMemObject(queue, clSource, volumeMap, 0, 0, 0);
    //clReleaseMemObject(clSource);
    clFinish(queue);
}
#endif

#if GL
#include "gl.h"

void projectGL(const ImageF& image, const GLTexture& volume, const Projection& projection) {
    SHADER(project)
    GLShader& program = projectShader;

    program["rotation"_] = projection.glRotation;
    program["origin"_] = projection.glOrigin;
    float halfHeight = float(volume.size.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
    program["plusMinusHalfHeightMinusOriginZ"_] = vec2(1,-1)*halfHeight - vec2(projection.glOrigin.z);
    float radius = float(volume.size.x-1)/2;
    program["c"_] = sq(projection.glOrigin.xy()) - radius*radius;
    program["radiusSq"_] = radius*radius;
    program["halfHeight"_] = halfHeight;
    program["dataOrigin"_] = projection.glOrigin + vec3(volume.size-int3(1,1,2/*FIXME*/))/2.f;

    program.bindSamplers({"volume"_}); volume.bind(0);
    GLFrameBuffer target = GLTexture(image.size());
    target.bind(ClearColor);

    GLVertexBuffer vertexBuffer;
    vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)});
    vertexBuffer.bindAttribute(program,"position"_,2);
    vertexBuffer.draw(TriangleStrip);

    target.texture.read(image);
}
#endif

#if AVX2
/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume = source;
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        v4sf start, step, end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) { row[x] = intersect(projection, vec2(x,y), volume, start, step, end) ? project(start, step, end, volume, source.data) : 0; }
    }, coreCount);
}
#endif
