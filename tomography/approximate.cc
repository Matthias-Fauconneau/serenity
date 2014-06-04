#include "approximate.h"
#include "sum.h"

KERNEL(approximate, backproject) // const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const mat4x3* projections, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects \a images to \a volume
void Approximate::backproject(const VolumeF& volume) {
    const float3 center = float3(p.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(images.size.xy()-int2(1))/2.f;
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0); // NVidia does not implement OpenCL 1.2 (2D image arrays)
    setKernelArgs(backprojectKernel, float4(center,0), radiusSq, imageCenter, size_t(projectionArray.size), projectionArray.data.pointer, images.data.pointer, sampler, volume.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, backprojectKernel, 3, 0, (size_t[]){(size_t)volume.size.x, (size_t)volume.size.y, (size_t)volume.size.z}, 0, 0, 0, 0), "backproject");
}


Approximate::Approximate(int3 volumeSize, const ref<Projection>& projections, const ImageArray& images, bool filter, bool regularize, string label)
    : Reconstruction(volumeSize, images.size, label+"-approximate"_+(filter?"-filter"_:""_)+(regularize?"-regularize"_:""_)), p(volumeSize), r(volumeSize), AtAp(volumeSize), filter(filter), regularize(regularize), projections(projections), projectionArray(projections, x.size, images.size.xy()), images(images) {
    /// Computes residual r=p=Atb (assumes x=0)
    backproject(p);
    clEnqueueCopyImage(queue, p.data.pointer, r.data.pointer, (size_t[]){0,0,0}, (size_t[]){0,0,0},  (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0,0,0);
    // Executes sum kernel
    residualEnergy = SSQ(r);
}

KERNEL(approximate, update)

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool Approximate::step() {
    totalTime.start();
    Time time; time.start();

    // Projects p
    for(uint projectionIndex: range(projections.size)) {
        project(images.data.pointer, projectionIndex*images.size.y*images.size.x, images.size.xy(), p, projections[projectionIndex]);
        //TODO: filter
    }
    backproject(AtAp);
    float pAtAp = dotProduct(p, AtAp); // Reduces to |p·Atp|
    float alpha = residualEnergy / pAtAp;
    setKernelArgs(updateKernel, x.data.pointer, 1, x.data.pointer, alpha, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0),  "Estimate: x = x + α p");
    setKernelArgs(updateKernel, r.data.pointer, 1,  r.data.pointer, -alpha, AtAp.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0), "Residual: r = r - α AtAp");
    float newResidual = dotProduct(r,r); // Reduces to |r|²
    float beta = newResidual / residualEnergy;
    setKernelArgs(updateKernel, p.data.pointer, 1, r.data.pointer, beta, p.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, updateKernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0), "Search direction: p = r + β p");

    time.stop();
    totalTime.stop();
    k++;
    log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    residualEnergy = newResidual;
    return true;
}
