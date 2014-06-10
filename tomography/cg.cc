#include "cg.h"
#include "sum.h"

ConjugateGradient::ConjugateGradient(int3 volumeSize, const ImageArray& b) : Reconstruction(volumeSize, b), p(volumeSize), r(volumeSize), Ap(b.size), AtAp(volumeSize), AAti(b.size) {
    buffer<float> ones (b.size.x*b.size.y*b.size.z); ones.clear(1); ImageArray i(b.size, ones); // Identity projections
    VolumeF Ati (volumeSize);
    backproject(Ati, At, i); // Backprojects identity
    project(AAti, Ati); // Re-projects backprojection of identity
    /// Computes residual r=p=Atb (assumes x=0)
    backproject(p, At, b);
    residualEnergy = SSQ(p);
    log("residualEnergy", residualEnergy);
    clEnqueueCopyImage(queue, p.data.pointer, r.data.pointer, (size_t[]){0,0,0}, (size_t[]){0,0,0},  (size_t[]){size_t(p.size.x), size_t(p.size.y), size_t(x.size.z)}, 0,0,0);
}

KERNEL(backproject, update)
KERNEL(backproject, divide)

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool ConjugateGradient::step() {
    totalTime.start();
    Time time; time.start();
    project(Ap, p);
    //setKernelArgs(divideKernel, images.data.pointer, images.data.pointer, AAti.data.pointer); // Normalizes
    //clCheck( clEnqueueNDRangeKernel(queue, divideKernel, 3, 0, (size_t[]){size_t(images.size.x), size_t(images.size.y), size_t(images.size.z)}, 0, 0, 0, 0), "Normalize: Ap = Ap / AAti");
    backproject(AtAp, At, Ap);
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
