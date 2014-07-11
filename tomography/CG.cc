#include "CG.h"
#include "operators.h"
#include "time.h"

CG::CG(const Projection& projection, ImageArray&& attenuation) : Reconstruction(projection, "CG"_), At(apply(attenuation.size.z, [&](uint index){ return projection.worldToDevice(index); }), "At"_), r(x.size, "r"_), p(x.size,"p"_), Ap(attenuation.size,"Ap"_), AtAp(x.size,"AtAp"_) {
    backproject(r, At, attenuation); // r = At b (x=0)
    residualEnergy = dot(r, r); // |r|²
    assert_(residualEnergy);
    copy(r, p); // r -> p
}

void CG::step() {
    time += project(Ap, A, p); // Ap = A p
    time += backproject(AtAp, At, Ap); // At Ap
    float pAtAp = dot(p, AtAp); // |p·AtAp|
    assert_(pAtAp);
    float alpha = residualEnergy / pAtAp;
    time += add(r, 1, r, -alpha, AtAp); // Residual: r = r - α AtAp
    time += maxadd(x, x, alpha, p); // Estimate: x = max(0, x + α p) (Constraining to positives values breaks conjugated gradient convergence properties but helps constrain the problem)
    float newResidual = dot(r, r); // |r|²
    float beta = newResidual / residualEnergy;
    time += add(p, 1, r, beta, p); //Search direction: p = r + β p
    residualEnergy = newResidual;
}
