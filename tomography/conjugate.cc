#include "conjugate.h"
#include "operators.h"
#include "time.h"

ConjugateGradient::ConjugateGradient(int3 volumeSize, const ImageArray& b) : Reconstruction(volumeSize, b), p(volumeSize), r(volumeSize), Ap(b.size), AtAp(volumeSize) {
     /// Computes residual r=p=Atb
    backproject(r, At, b); // p = At b (x=0)
    residualEnergy = SSQ(r);
    assert_(residualEnergy);
    copy(p, r);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
void ConjugateGradient::step() {
    time += project(Ap, p); // A p
    time += backproject(AtAp, At, Ap); // At Ap
    float pAtAp = dotProduct(p, AtAp); // |p·AtAp|
    assert_(pAtAp);
    float alpha = residualEnergy / pAtAp;
    time += add(r, 1, r, -alpha, AtAp); // Residual: r = r - α AtAp
    time += update(x, x, alpha, p); // Estimate: x = max(0, x + α p)
    float newResidual = SSQ(r); // |r|²
    float beta = newResidual / residualEnergy;
    time += add(p, 1, r, beta, p); //Search direction: p = r + β p
    residualEnergy = newResidual;
    k++;
}
