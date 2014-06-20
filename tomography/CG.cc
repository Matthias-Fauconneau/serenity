#include "CG.h"
#include "operators.h"
#include "time.h"

CG::CG(const Projection& projection, const ImageArray& b) : Reconstruction(projection, "CG"_), At(apply(b.size.z, [&](uint index){ return projection.worldToView(index); })), p(x.size), r(x.size), Ap(b.size), AtAp(x.size) {
     /// Computes residual r=p=Atb
    backproject(r, At, negln(b)); // p = At b (x=0)
    residualEnergy = SSQ(r);
    assert_(residualEnergy);
    copy(r, p); // r -> p
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
void CG::step() {
    time += project(Ap, A, p); // Ap = A p
    time += backproject(AtAp, At, Ap); // At Ap
    float pAtAp = dotProduct(p, AtAp); // |p·AtAp|
    assert_(pAtAp);
    float alpha = residualEnergy / pAtAp;
    time += add(r, 1, r, -alpha, AtAp); // Residual: r = r - α AtAp
    time += maxadd(x, x, alpha, p); // Estimate: x = max(0, x + α p)
    float newResidual = SSQ(r); // |r|²
    float beta = newResidual / residualEnergy;
    time += add(p, 1, r, beta, p); //Search direction: p = r + β p
    residualEnergy = newResidual;
    k++;
}
