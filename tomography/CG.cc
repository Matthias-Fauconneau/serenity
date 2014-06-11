#include "CG.h"
#include "sum.h"
#include "time.h"

ConjugateGradient::ConjugateGradient(int3 volumeSize, const ImageArray& b) : Reconstruction(volumeSize, b), p(volumeSize), r(volumeSize), Ap(b.size), AtAp(volumeSize) {
     /// Computes residual r=p=Atb
    backproject(r, At, b); // p = At b (x=0)
    clEnqueueCopyImage(queue, r.data.pointer, p.data.pointer, (size_t[]){0,0,0}, (size_t[]){0,0,0},  (size_t[]){size_t(r.size.x), size_t(r.size.y), size_t(r.size.z)}, 0,0,0); // p = r
    residualEnergy = SSQ(r);
}

// y := alpha * a + beta * b;
KERNEL(apply, apply)
void apply(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) {
    setKernelArgs(applyKernel, y.data.pointer, alpha, a.data.pointer, beta, b.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, applyKernel, 3, 0, (size_t[]){size_t(y.size.x), size_t(y.size.y), size_t(y.size.z)}, 0, 0, 0, 0), "Apply: y = α a + β b");
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
void ConjugateGradient::step() {
    Time time;

    project(Ap, p); // A p
    backproject(AtAp, At, Ap); // At Ap
    float pAtAp = dotProduct(p, AtAp); // |p·AtAp|
    float alpha = residualEnergy / pAtAp;
    apply(r, 1, r, -alpha, AtAp); // Residual: r = r - α AtAp
    apply(x, 1, x, alpha, p); // Estimate: x = x + α p
    float newResidual = SSQ(r); // |r|²
    float beta = newResidual / residualEnergy;
    apply(p, 1, r, beta, p); //Search direction: p = r + β p
    residualEnergy = newResidual;

    k++; totalTime += time;
}
