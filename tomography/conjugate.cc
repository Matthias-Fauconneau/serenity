#include "conjugate.h"
#include "sum.h"
#include "time.h"

ConjugateGradient::ConjugateGradient(int3 volumeSize, const ImageArray& b) : Reconstruction(volumeSize, b), p(volumeSize), r(volumeSize), Ap(b.size), AtAp(volumeSize) {
     /// Computes residual r=p=Atb
    backproject(r, At, b); // p = At b (x=0)
    residualEnergy = SSQ(r);
    assert_(residualEnergy);
    copy(p, r);
}

CL(operators, add) void add(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) { CL::add(y.size, y, alpha, a, beta, b); } // y = α a + β b

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
void ConjugateGradient::step() {
    Time time;

    project(Ap, p); // A p
    backproject(AtAp, At, Ap); // At Ap
    float pAtAp = dotProduct(p, AtAp); // |p·AtAp|
    assert_(pAtAp);
    float alpha = residualEnergy / pAtAp;
    add(r, 1, r, -alpha, AtAp); // Residual: r = r - α AtAp
    add(x, 1, x, alpha, p); // Estimate: x = x + α p
    float newResidual = SSQ(r); // |r|²
    float beta = newResidual / residualEnergy;
    add(p, 1, r, beta, p); //Search direction: p = r + β p
    residualEnergy = newResidual;

    k++; totalTime += time;
}
