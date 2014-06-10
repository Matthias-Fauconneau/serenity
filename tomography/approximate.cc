#include "approximate.h"
#include "sum.h"

KERNEL(approximate, backproject) //const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const struct mat4* worldToView, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects \a images to \a volume
void Approximate::backproject(const ImageArray& images, const VolumeF& volume) {
    const float3 center = float3(volume.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(images.size.xy()-int2(1))/2.f;
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP, CL_FILTER_LINEAR, 0); // NVidia does not implement OpenCL 1.2 (2D image arrays)
    setKernelArgs(backprojectKernel, float4(center,0), radiusSq, imageCenter, size_t(projectionArray.size), projectionArray.data.pointer, images.data.pointer, sampler, volume.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, backprojectKernel, 3, 0, (size_t[]){size_t(volume.size.x), size_t(volume.size.y), size_t(volume.size.z)}, 0, 0, 0, 0), "backproject");
}

void Approximate::project(const ImageArray& images, const VolumeF& volume) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, images.size.y*images.size.x*sizeof(float), 0, 0);
    for(uint projectionIndex: range(projections.size)) {
        //project(images.data.pointer, projectionIndex*images.size.y*images.size.x, images.size.xy(), p, projections[projectionIndex]); //FIXME
        ::project(buffer, 0, images.size.xy(), volume, projections[projectionIndex]);
        clEnqueueCopyBufferToImage(queue, buffer, images.data.pointer, 0, (size_t[]){0,0,projectionIndex}, (size_t[]){size_t(images.size.x),size_t(images.size.y),size_t(1)}, 0,0,0);
    }
    clReleaseMemObject(buffer);
}

Approximate::Approximate(int3 volumeSize, const ref<Projection>& projections, const ImageArray& images, bool filter, bool regularize, string label)
    : Reconstruction(volumeSize, images.size, label+"-approximate"_+(filter?"-filter"_:""_)+(regularize?"-regularize"_:""_)), p(volumeSize), r(volumeSize), AtAp(volumeSize), filter(filter), regularize(regularize), projections(projections), projectionArray(projections), images(images) {
    buffer<float> ones (images.size.x*images.size.y*images.size.z); ones.clear(1);
    w = ImageArray(images.size, ones);
    backproject(p, w); // Backprojects 1's on volume
    project(w, p); // Projects to weighting images
    /// Computes residual r=p=Atb (assumes x=0)
    backproject(images, p);
    residualEnergy = SSQ(p);
    log("residualEnergy", residualEnergy);
    clEnqueueCopyImage(queue, p.data.pointer, r.data.pointer, (size_t[]){0,0,0}, (size_t[]){0,0,0},  (size_t[]){size_t(p.size.x), size_t(p.size.y), size_t(x.size.z)}, 0,0,0);
}

KERNEL(approximate, update)
KERNEL(approximate, divide)

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool Approximate::step() {
    totalTime.start();
    Time time; time.start();
    project(images, p);
    //setKernelArgs(divideKernel, images.data.pointer, images.data.pointer, w.data.pointer); // Normalizes
    //clCheck( clEnqueueNDRangeKernel(queue, divideKernel, 3, 0, (size_t[]){size_t(images.size.x), size_t(images.size.y), size_t(images.size.z)}, 0, 0, 0, 0), "Normalize: Ap = Ap / w");
    backproject(images, AtAp);
    float pAtAp = dotProduct(p, AtAp); // Reduces to |p·AtAp|
    log("pAtAp", pAtAp);
    float alpha = residualEnergy / pAtAp;
    log("alpha", alpha);
    assert_(alpha);
    setKernelArgs(updateKernel, r.data.pointer, 1.f,  r.data.pointer, -alpha, AtAp.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){size_t(r.size.x), size_t(r.size.y), size_t(r.size.z)}, 0, 0, 0, 0), "Residual: r = r - α AtAp");
    setKernelArgs(updateKernel, x.data.pointer, 1.f, x.data.pointer, alpha, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){size_t(x.size.x), size_t(x.size.y), size_t(x.size.z)}, 0, 0, 0, 0),  "Estimate: x = x + α p");
    float newResidual = SSQ(r); // Reduces to |r|²
    log("newResidual ", newResidual);
    float beta = newResidual / residualEnergy;
    log("beta", beta);
    setKernelArgs(updateKernel, p.data.pointer, 1.f, r.data.pointer, beta, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){size_t(p.size.x), size_t(p.size.y), size_t(p.size.z)}, 0, 0, 0, 0), "Search direction: p = r + β p");

    time.stop();
    totalTime.stop();
    k++;
    log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    residualEnergy = newResidual;
    return true;
}
