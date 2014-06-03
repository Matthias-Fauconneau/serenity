#include "approximate.h"
#include "sum.h"

KERNEL(approximate, backproject) //const mat4x3* projections, __read_only image2d_array_t images, sampler_t imageSampler, image3d_t p, image3d_t r) {

/// Backprojects \a images to \a volume
void Approximate::backproject(const VolumeF& volume) {
    cl_kernel kernel = ::backproject();
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0); // NVidia does not implement OpenCL 1.2 (2D image arrays)
    setKernelArgs(kernel, projectionArray.data.pointer, images, sampler, volume.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 3, 0, (size_t[]){(size_t)volume.size.x, (size_t)volume.size.y, (size_t)volume.size.z}, 0, 0, 0, 0) );
}


Approximate::Approximate(int3 reconstructionSize, const ref<Projection>& projections, const ImageArray& images, bool filter, bool regularize, string label)
    : Reconstruction(reconstructionSize, images.size, label), p(reconstructionSize), r(reconstructionSize), AtAp(reconstructionSize), filter(filter), regularize(regularize), projections(projections), projectionArray(projections, x.size, images.size.xy()), images(images) {
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
    float pAtAp = dot(p, AtAp); // Reduces to |p·Atp|
    float alpha = residualEnergy / pAtAp;
    cl_kernel kernel = update();
    setKernelArgs(kernel, x.data.pointer, 1, x.data.pointer, alpha, p.data.pointer);       // Estimate: x = x + α p
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0) );
    setKernelArgs(kernel, r.data.pointer, 1,  r.data.pointer, -alpha, AtAp.data.pointer); // Residual: r = r - α AtAp
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0) );
    float newResidual = dot(r,r); // Reduces to |r|²
    float beta = newResidual / residualEnergy;
    setKernelArgs(kernel, p.data.pointer, 1, r.data.pointer, beta, p.data.pointer); // Search direction: p = r + β p
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 3, 0, (size_t[]){(size_t)x.size.x, (size_t)x.size.y, (size_t)x.size.z}, 0, 0, 0, 0) );

    time.stop();
    totalTime.stop();
    k++;
    log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    residualEnergy = newResidual;
    return true;
}
