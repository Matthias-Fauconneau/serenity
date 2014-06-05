#include "approximate.h"
#include "sum.h"

KERNEL(approximate, backproject) // const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const mat4x3* projections, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects \a images to \a volume
void Approximate::backproject(const VolumeF& A) {
    const float3 center = float3(p.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(images.size.xy()-int2(1))/2.f;
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP, CL_FILTER_LINEAR, 0); // NVidia does not implement OpenCL 1.2 (2D image arrays)

    {
        ::buffer<float> data (images.size.x * images.size.y * images.size.z);
        clCheck( clEnqueueReadImage(queue, images.data.pointer, true, (size_t[]){0,0,0}, (size_t[]){size_t(images.size.x),size_t(images.size.y),size_t(images.size.z)}, 0,0, (float*)data.data, 0,0,0) );
        for(float x: data) assert_(isNumber(x), x, "images");
    }

    // FIXME
    //cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, A.size.x*A.size.y*A.size.z * sizeof(float), 0, 0);
    //clCheck( clEnqueueCopyImageToBuffer(queue, A.data.pointer, buffer, (size_t[]){0,0,0}, (size_t[]){size_t(A.size.x),size_t(A.size.y),size_t(A.size.z)}, 0,0,0,0) ); // FIXME

    setKernelArgs(backprojectKernel, float4(center,0), radiusSq, imageCenter, size_t(projectionArray.size), projectionArray.data.pointer, images.data.pointer, sampler, A.data.pointer);
    //setKernelArgs(backprojectKernel, float4(center,0), radiusSq, imageCenter, size_t(projectionArray.size), projectionArray.data.pointer, images.data.pointer, sampler, size_t(A.size.y*A.size.x), size_t(A.size.x), buffer);
    clCheck( clEnqueueNDRangeKernel(queue, backprojectKernel, 3, 0, (size_t[]){(size_t)A.size.x, (size_t)A.size.y, (size_t)A.size.z}, 0, 0, 0, 0), "backproject");

    //clCheck( clEnqueueCopyBufferToImage(queue, buffer, A.data.pointer, 0, (size_t[]){0,0,0}, (size_t[]){size_t(images.size.x),size_t(images.size.y),size_t(images.size.z)}, 0,0,0) ); // FIXME
    {
        ::buffer<float> data (A.size.x * A.size.y * A.size.z);
        clCheck( clEnqueueReadImage(queue, A.data.pointer, true, (size_t[]){0,0,0}, (size_t[]){size_t(A.size.x),size_t(A.size.y),size_t(A.size.z)}, 0,0, (float*)data.data, 0,0,0) );
        for(float x: data) assert_(isNumber(x), x, "backproject");
    }
}


Approximate::Approximate(int3 volumeSize, const ref<Projection>& projections, const ImageArray& images, bool filter, bool regularize, string label)
    : Reconstruction(volumeSize, images.size, label+"-approximate"_+(filter?"-filter"_:""_)+(regularize?"-regularize"_:""_)), p(volumeSize), r(volumeSize), AtAp(volumeSize), filter(filter), regularize(regularize), projections(projections), projectionArray(projections, x.size, images.size.xy()), images(images) {
    /// Computes residual r=p=Atb (assumes x=0)
    backproject(p);
    clEnqueueCopyImage(queue, p.data.pointer, r.data.pointer, (size_t[]){0,0,0}, (size_t[]){0,0,0},  (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0,0,0);
    // Executes sum kernel
    residualEnergy = SSQ(r);
    log("residualEnergy", residualEnergy);
}

KERNEL(approximate, update)

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool Approximate::step() {
    totalTime.start();
    Time time; time.start();

    // Projects p
    for(uint projectionIndex: range(projections.size)) {
        //project(images.data.pointer, projectionIndex*images.size.y*images.size.x, images.size.xy(), p, projections[projectionIndex]); FIXME
        cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, images.size.y*images.size.x * sizeof(float), 0, 0);
        project(buffer, 0, images.size.xy(), p, projections[projectionIndex]);
        clEnqueueCopyBufferToImage(queue, buffer, images.data.pointer, 0, (size_t[]){0,0,projectionIndex}, (size_t[]){size_t(images.size.x),size_t(images.size.y),size_t(1)}, 0,0,0);
    }
    backproject(AtAp);
    float pAtAp = dotProduct(p, AtAp); // Reduces to |p·Atp|
    log("pAtAp", pAtAp);
    float alpha = residualEnergy / pAtAp;
    log("alpha", alpha);
    setKernelArgs(updateKernel, x.data.pointer, 1, x.data.pointer, alpha, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0),  "Estimate: x = x + α p");
    setKernelArgs(updateKernel, r.data.pointer, 1,  r.data.pointer, -alpha, AtAp.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0), "Residual: r = r - α AtAp");
    float newResidual = dotProduct(r,r); // Reduces to |r|²
    log("newResidual ", newResidual);
    float beta = newResidual / residualEnergy;
    log("beta", beta);
    setKernelArgs(updateKernel, p.data.pointer, 1, r.data.pointer, beta, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0), "Search direction: p = r + β p");

    time.stop();
    totalTime.stop();
    k++;
    log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    residualEnergy = newResidual;
    return true;
}
