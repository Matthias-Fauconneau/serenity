// TODO: Face contacts
#include "simulation.h"
#include "parallel.h"
#include "grain.h"
#include "membrane.h"

#if MEMBRANE_FACE
static inline bool pointTriangle(float Ox, float Oy, float Oz, float e0x, float e0y, float e0z,
                                 float e1x, float e1y, float e1z, float& d2) {
 const float Nx = e0y*e1z - e1y*e0z;
 const float Ny = e0z*e1x - e1z*e0x;
 const float Nz = e0x*e1y - e1x*e0y;
 const float Px = Ny*e1z - e1y*Nz;
 const float Py = Nz*e1x - e1z*Nx;
 const float Pz = Nx*e1y - e1x*Ny;
 const float det = Px * e0x + Py * e0y + Pz * e0z;
 const float invDet = 1 / det;
 float u = invDet * (Px*Ox + Py*Oy + Pz*Oz);
 if(u < 0) u=0; //return false; // FIXME: u/v edge contacts are doubled
 const float Qx = Oy*e0z - e0y*Oz;
 const float Qy = Oz*e0x - e0z*Ox;
 const float Qz = Ox*e0y - e0x*Oy;
 float v = invDet * (Qx*Nx + Qy*Ny + Qz*Nz);
 if(v < 0) v=0; //return false; // FIXME: u/v edge contacts are doubled
 if(u + v > 1) return false; // Any edge contact is handled by neighbouring face's u=0 or v=0 edge
 float t = invDet * (Qx*e1x + Qy*e1y + Qz*e1z);
 if(t > 0) return false;
 if(u > 0 && v > 0) { // If u nor v are constrained to edge, this is the normal line (face contacts)
  const float N2 = Nx*Nx + Ny*Ny + Nz*Nz;
  d2 = sq(-t) * N2;
 } else { // Edge contacts are not along the face normal
  // Contact point
  float X = u*e0x + v*e1x;
  float Y = u*e0y + v*e1y;
  float Z = u*e0z + v*e1z;
  d2 = sq(Ox-X) + sq(Oy-Y) + sq(Oz-Z);
 }
 return true;
}

static inline void pointTriangle(float Ox, float Oy, float Oz, float e0x, float e0y, float e0z,
                                 float e1x, float e1y, float e1z,
                                 float& d, float& Nx, float& Ny, float& Nz,
                                 float& u, float& v, float& t) {
 Nx = e0y*e1z - e1y*e0z;
 Ny = e0z*e1x - e1z*e0x;
 Nz = e0x*e1y - e1x*e0y;
 const float Px = Ny*e1z - e1y*Nz;
 const float Py = Nz*e1x - e1z*Nx;
 const float Pz = Nx*e1y - e1x*Ny;
 const float det = Px * e0x + Py * e0y + Pz * e0z;
 const float invDet = 1 / det;
 u = invDet * (Px*Ox + Py*Oy + Pz*Oz);
 if(u < 0) u=0; // FIXME: u/v edge contacts are doubled
 const float Qx = Oy*e0z - e0y*Oz;
 const float Qy = Oz*e0x - e0z*Ox;
 const float Qz = Ox*e0y - e0x*Oy;
 v = invDet * (Qx*Nx + Qy*Ny + Qz*Nz);
 if(v < 0) v=0; // FIXME: u/v edge contacts are doubled
 //if(u + v > 1) return false; // Any edge contact is handled by neighbouring face's u=0 or v=0 edge
 t = invDet * (Qx*e1x + Qy*e1y + Qz*e1z);
 //if(t > 0) return false;
 if(u > 0 && v > 0) { // If u nor v are constrained to edge, this is the normal line (face contacts)
  const float N2 = Nx*Nx + Ny*Ny + Nz*Nz;
  const float N = sqrt(N2);
  d = -t * N;
  Nx /= N;
  Ny /= N;
  Nz /= N;
 } else { // Edge contacts are not along the face normal
  // Contact point
  float X = u*e0x + v*e1x;
  float Y = u*e0y + v*e1y;
  float Z = u*e0z + v*e1z;
  Nx = Ox-X;
  Ny = Oy-Y;
  Nz = Oz-Z;
  float d2 = sq(Nx) + sq(Ny) + sq(Nz);
  d = sqrt(d2);
  Nx /= d;
  Ny /= d;
  Nz /= d;
 }
}
#endif

static inline void evaluateGrainMembrane(const int start, const int size,
                                         const int* pContacts, const int unused contactCount,
                                         const int* gmA, const int* gmB,
                                         const float* grainPx, const float* grainPy, const float* grainPz,
                                         const float* membranePx, const float* membranePy, const float* membranePz,
                                         #if MEMBRANE_FACE
                                         const int margin, const int stride,
                                         #endif
                                         const float Gr,
                                         float* const LocalAx, float* const LocalAy, float* const LocalAz,
                                         #if MEMBRANE_FACE
                                         float* const LocalBu, float* const LocalBv, float* const LocalBt,
                                         #endif
                                         const vXsf K, const vXsf Kb,
                                         const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                         const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                         const vXsf staticFrictionDamping,
                                         const float* AVx, const float* AVy, const float* AVz,
                                         const float* pBVx, const float* pBVy, const float* pBVz,
                                         const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                         const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                         const float* ArotationW,
                                         float* const pFx, float* const pFy, float* const pFz,
                                         float* const pTAx, float* const pTAy, float* const pTAz,
                                         float* const pU, float* const pV, int I) {
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = load(pContacts, i);
  const vXsi A = gather(gmA, contacts), B = gather(gmB, contacts);
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
#define MEMBRANE_POINT !MEMBRANE_FACE
#if MEMBRANE_POINT
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Bx = gather(membranePx, B), By = gather(membranePy, B), Bz = gather(membranePz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Gr - length;
  const vXsf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
#else
  vXsf Rx, Ry, Rz, U, V, T, BVx, BVy, BVz, TNx, TNy, TNz, B0x, B0y, B0z, B1x, B1y, B1z, B2x, B2y, B2z, depth;
  vXsf Nx, Ny, Nz;
  for(int k: range(simd)) {
   if(i+k >= contactCount) break; // FIXME: pad values produces NaN in ::nearest
   const int b = B[k];
   const float V0x = membranePx[b], V0y = membranePy[b], V0z = membranePz[b];
   const float Ox = Ax[k]-V0x, Oy = Ay[k]-V0y, Oz = Az[k]-V0z;
   const int rowIndex = (b-margin)/stride;
   const int e0 = -stride+rowIndex%2;
   const int e1 = e0-1;
   const int e2 = -1;
   if(I == 0) { // (.,0,1)
    const int e0v = b+e0;
    const int e1v = b+e1;
    const float e0x = membranePx[e0v]-V0x, e0y = membranePy[e0v]-V0y, e0z = membranePz[e0v]-V0z;
    const float e1x = membranePx[e1v]-V0x, e1y = membranePy[e1v]-V0y, e1z = membranePz[e1v]-V0z;
    float nx, ny, nz, d, u, v, t;
    pointTriangle(Ox, Oy, Oz, e0x, e0y, e0z, e1x, e1y, e1z, d, nx, ny, nz, u, v, t);
    Nx[k] = nx;
    Ny[k] = ny;
    Nz[k] = nz;
    Rx[k] = d*nx;
    Ry[k] = d*ny;
    Rz[k] = d*nz;
    depth[k] = Gr - d;
    U[k] = u;
    V[k] = v;
    T[k] = t;
    TNx[k] = e0y*e1z - e1y*e0z;
    TNy[k] = e0z*e1x - e1z*e0x;
    TNz[k] = e0x*e1y - e1x*e0y;
    B0x[k] = V0x;
    B0y[k] = V0y;
    B0z[k] = V0z;
    B1x[k] = membranePx[e0v];
    B1y[k] = membranePy[e0v];
    B1z[k] = membranePz[e0v];
    B2x[k] = membranePx[e1v];
    B2y[k] = membranePy[e1v];
    B2z[k] = membranePz[e1v];
    const float BV0x = pBVx[b], BV0y = pBVy[b], BV0z = pBVz[b];
    const float BV1x = pBVx[e0v], BV1y = pBVy[e0v], BV1z = pBVz[e0v];
    const float BV2x = pBVx[e1v], BV2y = pBVy[e1v], BV2z = pBVz[e1v];
    const float w = 1-u-v;
    BVx[k] = w*BV0x + u*BV1x + v*BV2x;
    BVy[k] = w*BV0y + u*BV1y + v*BV2y;
    BVz[k] = w*BV0z + u*BV1z + v*BV2z;
   } else { // (.,1,2)
    const int e1v = b+e1;
    const int e2v = b+e2;
    const float e1x = membranePx[e1v]-V0x, e1y = membranePy[e1v]-V0y, e1z = membranePz[e1v]-V0z;
    const float e2x = membranePx[e2v]-V0x, e2y = membranePy[e2v]-V0y, e2z = membranePz[e2v]-V0z;
    float nx, ny, nz, d, u, v, t;
    pointTriangle(Ox, Oy, Oz, e1x, e1y, e1z, e2x, e2y, e2z, d, nx, ny, nz, u, v, t);
    Nx[k] = nx;
    Ny[k] = ny;
    Nz[k] = nz;
    Rx[k] = d*nx;
    Ry[k] = d*ny;
    Rz[k] = d*nz;
    depth[k] = Gr - d;
    U[k] = u;
    V[k] = v;
    T[k] = t;
    TNx[k] = e1y*e2z - e2y*e1z;
    TNy[k] = e1z*e2x - e2z*e1x;
    TNz[k] = e1x*e2y - e2x*e1y;
    B0x[k] = V0x;
    B0y[k] = V0y;
    B0z[k] = V0z;
    B1x[k] = membranePx[e1v];
    B1y[k] = membranePy[e1v];
    B1z[k] = membranePz[e1v];
    B2x[k] = membranePx[e2v];
    B2y[k] = membranePy[e2v];
    B2z[k] = membranePz[e2v];
    const float BV0x = pBVx[b], BV0y = pBVy[b], BV0z = pBVz[b];
    const float BV1x = pBVx[e1v], BV1y = pBVy[e1v], BV1z = pBVz[e1v];
    const float BV2x = pBVx[e2v], BV2y = pBVy[e2v], BV2z = pBVz[e2v];
    const float w = 1-u-v;
    BVx[k] = w*BV0x + u*BV1x + v*BV2x;
    BVy[k] = w*BV0y + u*BV1y + v*BV2y;
    BVz[k] = w*BV0z + u*BV1z + v*BV2z;
   }
  }
  const vXsf Bx = Ax-Rx, By = Ay-Ry, Bz = Az-Rz;
#endif
  const vXsf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - BVx;
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - BVy;
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - BVz;
  // Damping
  const vXsf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  const vXsf Fb = - Kb * sqrt(sqrt(depth)) * normalSpeed ; // Damping
  // Normal force
  const vXsf Fn = Fk + Fb;
  const vXsf NFx = Fn * Nx;
  const vXsf NFy = Fn * Ny;
  const vXsf NFz = Fn * Nz;

  // Dynamic friction
  // Tangent relative velocity
  const vXsf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  const vXsf TRVx = RVx - RVn * Nx;
  const vXsf TRVy = RVy - RVn * Ny;
  const vXsf TRVz = RVz - RVn * Nz;
  const vXsf tangentRelativeSpeed = sqrt(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  const maskX div0 = greaterThan(tangentRelativeSpeed, _0f);
  const vXsf Fd = - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = mask3_fmadd(Fd, TRVx, _0f, div0);
  const vXsf FDy = mask3_fmadd(Fd, TRVy, _0f, div0);
  const vXsf FDz = mask3_fmadd(Fd, TRVz, _0f, div0);

  // Gather static frictions
  const vXsf oldLocalAx = gather(LocalAx, contacts);
  const vXsf oldLocalAy = gather(LocalAy, contacts);
  const vXsf oldLocalAz = gather(LocalAz, contacts);
#if MEMBRANE_FACE
  const vXsf oldLocalBu = gather(LocalBu, contacts);
  const vXsf oldLocalBv = gather(LocalBv, contacts);
  const vXsf oldLocalBt = gather(LocalBt, contacts);
#endif

  const vXsf QAx = gather(ArotationX, A);
  const vXsf QAy = gather(ArotationY, A);
  const vXsf QAz = gather(ArotationZ, A);
  const vXsf QAw = gather(ArotationW, A);
  const vXsf X1 = QAw*RAx + RAy*QAz - QAy*RAz;
  const vXsf Y1 = QAw*RAy + RAz*QAx - QAz*RAx;
  const vXsf Z1 = QAw*RAz + RAx*QAy - QAx*RAy;
  const vXsf W1 = - (RAx * QAx + RAy * QAy + RAz * QAz);
  const vXsf newLocalAx = QAw*X1 - (W1*QAx + QAy*Z1 - Y1*QAz);
  const vXsf newLocalAy = QAw*Y1 - (W1*QAy + QAz*X1 - Z1*QAx);
  const vXsf newLocalAz = QAw*Z1 - (W1*QAz + QAx*Y1 - X1*QAy);

#if MEMBRANE_FACE
  const vXsf newLocalBu = U;
  const vXsf newLocalBv = V;
  const vXsf newLocalBt = T;
#endif

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
#if MEMBRANE_FACE
  const vXsf localBu = blend(reset, oldLocalBu, newLocalBu);
  const vXsf localBv = blend(reset, oldLocalBv, newLocalBv);
  const vXsf localBt = blend(reset, oldLocalBt, newLocalBt);
#endif

  const vXsf X = QAw*localAx - (localAy*QAz - QAy*localAz);
  const vXsf Y = QAw*localAy - (localAz*QAx - QAz*localAx);
  const vXsf Z = QAw*localAz - (localAx*QAy - QAx*localAy);
  const vXsf W = localAx * QAx + localAy * QAy + localAz * QAz;
  const vXsf FRAx = QAw*X + W*QAx + QAy*Z - Y*QAz;
  const vXsf FRAy = QAw*Y + W*QAy + QAz*X - Z*QAx;
  const vXsf FRAz = QAw*Z + W*QAz + QAx*Y - X*QAy;
#if MEMBRANE_FACE
  const vXsf localBw = _1f - localBu - localBv;
  const vXsf FRBx = localBw * B0x + localBu * B1x + localBv * B2x + localBt * TNx;
  const vXsf FRBy = localBw * B0y + localBu * B1y + localBv * B2y + localBt * TNy;
  const vXsf FRBz = localBw * B0z + localBu * B1z + localBv * B2z + localBt * TNz;
#endif

  const vXsf gAx = Ax + FRAx;
  const vXsf gAy = Ay + FRAy;
  const vXsf gAz = Az + FRAz;
#if MEMBRANE_FACE
  const vXsf gBx = Bx + FRBx;
  const vXsf gBy = By + FRBy;
  const vXsf gBz = Bz + FRBz;
#else
  const vXsf gBx = Bx;
  const vXsf gBy = By;
  const vXsf gBz = Bz;
#endif
  const vXsf Dx = gBx - gAx;
  const vXsf Dy = gBy - gAy;
  const vXsf Dz = gBz - gAz;
  const vXsf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // Tangent offset
  const vXsf TOx = Dx - Dn * Nx;
  const vXsf TOy = Dy - Dn * Ny;
  const vXsf TOz = Dz - Dn * Nz;
  const vXsf tangentLength = sqrt(TOx*TOx+TOy*TOy+TOz*TOz);
  const vXsf Ks = staticFrictionStiffness * Fn;
  const vXsf Fs = Ks * tangentLength; // 0.1~1 fN
  // Spring direction
  const vXsf SDx = TOx / tangentLength;
  const vXsf SDy = TOy / tangentLength;
  const vXsf SDz = TOz / tangentLength;
  const maskX hasTangentLength = greaterThan(tangentLength, _0f);
  const vXsf sfFb = staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz);
  const maskX hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
    & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const vXsf sfFt = maskSub(Fs, hasTangentLength, sfFb);
  const vXsf FTx = mask3_fmadd(sfFt, SDx, FDx, hasStaticFriction & hasTangentLength);
  const vXsf FTy = mask3_fmadd(sfFt, SDy, FDy, hasStaticFriction & hasTangentLength);
  const vXsf FTz = mask3_fmadd(sfFt, SDz, FDz, hasStaticFriction & hasTangentLength);
  // Resets contacts without static friction
  localAx = blend(hasStaticFriction, _0f, localAx); // FIXME use 1s (NaN) not 0s to flag resets
  store(pFx, i, NFx + FTx);
  store(pFy, i, NFy + FTy);
  store(pFz, i, NFz + FTz);
  store(pTAx, i, RAy*FTz - RAz*FTy);
  store(pTAy, i, RAz*FTx - RAx*FTz);
  store(pTAz, i, RAx*FTy - RAy*FTx);
  store(pU, i, U);
  store(pV, i, V);

  // Scatter static frictions
  scatter(LocalAx, contacts, localAx);
  scatter(LocalAy, contacts, localAy);
  scatter(LocalAz, contacts, localAz);
#if MEMBRANE_FACE
  scatter(LocalBu, contacts, localBu);
  scatter(LocalBv, contacts, localBv);
  scatter(LocalBt, contacts, localBt);
#endif
 }
}

void Simulation::stepGrainMembrane() {
 if(!grain->count || !membrane->count) return;
 if(grainMembraneGlobalMinD <= 0)  {
  const float verletDistance = 2*Grain::radius/sqrt(3.);
  assert(verletDistance > Grain::radius + 0);

  for(size_t I: range(2)) { // Face type
   auto& gm = grainMembrane[I];
   swap(gm.oldA, gm.A);
   swap(gm.oldB, gm.B);
   swap(gm.oldLocalAx, gm.localAx);
   swap(gm.oldLocalAy, gm.localAy);
   swap(gm.oldLocalAz, gm.localAz);
#if MEMBRANE_FACE
   swap(gm.oldLocalBu, gm.localBu);
   swap(gm.oldLocalBv, gm.localBv);
   swap(gm.oldLocalBt, gm.localBt);
#endif

   static constexpr size_t averageContactCount = 1;
   const size_t GWcc = align(simd, grain->count * averageContactCount +1);
   if(GWcc > gm.A.capacity) {
    memoryTime.start();
    gm.A = buffer<int>(GWcc, 0);
    gm.B = buffer<int>(GWcc, 0);
    gm.localAx = buffer<float>(GWcc, 0);
    gm.localAy = buffer<float>(GWcc, 0);
    gm.localAz = buffer<float>(GWcc, 0);
#if MEMBRANE_FACE
    gm.localBu = buffer<float>(GWcc, 0);
    gm.localBv = buffer<float>(GWcc, 0);
    gm.localBt = buffer<float>(GWcc, 0);
#endif
    memoryTime.stop();
   }
   gm.A.size = gm.A.capacity;
   gm.B.size = gm.B.capacity;

   atomic contactCount;
#if MEMBRANE_POINT
   grainLattice();

   const int X = lattice.size.x, Y = lattice.size.y;
   const int* latticeNeighbours[3*3] = {
    lattice.base.data+(-X*Y-X-1),
    lattice.base.data+(-X*Y-1),
    lattice.base.data+(-X*Y+X-1),

    lattice.base.data+(-X-1),
    lattice.base.data+(-1),
    lattice.base.data+(X-1),

    lattice.base.data+(X*Y-X-1),
    lattice.base.data+(X*Y-1),
    lattice.base.data+(X*Y+X-1)
   };

   auto search = [this, &latticeNeighbours, verletDistance, &contactCount](uint, uint rowIndex) {
    const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
    const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
    int* const gmA = A.begin(), *gmB = B.begin();
    const vXsf scale = floatX(lattice.scale);
    const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
    const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
    const vXsf sqVerletDistanceX = floatX(sq(verletDistance));
    const vXsi _1i = intX(-1);
    int W = membrane->W;
    int base = membrane->margin+rowIndex*membrane->stride;
    for(int j=0; j<W; j+=simd) {
     int i = base+j;
     vXsi b = intX(i)+_seqi;
     const vXsf Bx = load(mPx, i), By = load(mPy, i), Bz = load(mPz, i);
     vXsi index = convert(scale*(Bz-minZ)) * sizeYX
       + convert(scale*(By-minY)) * sizeX
       + convert(scale*(Bx-minX));
     // Neighbours
     for(int n: range(3*3)) for(int i: range(3)) {
      vXsi a = gather(latticeNeighbours[n]+i, index);
      const vXsf Ax = gather(gPx, a), Ay = gather(gPy, a), Az = gather(gPz, a);
      const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
      vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
      maskX mask = notEqual(a, _1i) & lessThan(sqDistance, sqVerletDistanceX);
      uint targetIndex = contactCount.fetchAdd(countBits(mask));
      compressStore(gmA+targetIndex, mask, a);
      compressStore(gmB+targetIndex, mask, b);
     }
    }
   };
   grainMembraneSearchTime += parallel_for(0, membrane->H, search);
#else // Face
   // TODO: Face with Lattice

   auto search = [&](uint, uint rowIndex) {
    const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
    const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
    int* const gmA = gm.A.begin(), *gmB = gm.B.begin();
    const float sqVerletDistance = sq(verletDistance);
    const int W = membrane->W;
    const int stride = membrane->stride;
    const int base = membrane->margin+rowIndex*stride;
    const int e0 = -stride+rowIndex%2;
    const int e1 = e0-1;
    const int e2 = -1;
    for(int j=0; j<W; j++) {
     const int b = base+j;
     const float V0x = mPx[b];
     const float V0y = mPy[b];
     const float V0z = mPz[b];
     const int e0v = b+e0;
     const int e1v = b+e1;
     const int e2v = b+e2;
     const float e0x = mPx[e0v]-V0x, e0y = mPy[e0v]-V0y, e0z = mPz[e0v]-V0z;
     const float e1x = mPx[e1v]-V0x, e1y = mPy[e1v]-V0y, e1z = mPz[e1v]-V0z;
     const float e2x = mPx[e2v]-V0x, e2y = mPy[e2v]-V0y, e2z = mPz[e2v]-V0z;
     if(I==0) { // (.,0,1)
      for(int a: range(grain->count)) {
       const float Ax = gPx[a], Ay = gPy[a], Az = gPz[a];
       const float Rx = Ax-V0x, Ry = Ay-V0y, Rz = Az-V0z;
       float sqDistance;
       if(pointTriangle(Rx, Ry, Rz, e0x, e0y, e0z, e1x, e1y, e1z, sqDistance)) {
        bool mask = sqDistance < sqVerletDistance;
        if(mask) {
         uint targetIndex = contactCount.fetchAdd(1);
         gmA[targetIndex] = a;
         gmB[targetIndex] = b;
        }
       }
      }
     } else { // (.,1,2)
      for(int a: range(grain->count)) {
       const float Ax = gPx[a], Ay = gPy[a], Az = gPz[a];
       const float Rx = Ax-V0x, Ry = Ay-V0y, Rz = Az-V0z;
       float sqDistance;
       if(pointTriangle(Rx, Ry, Rz, e1x, e1y, e1z, e2x, e2y, e2z, sqDistance)) {
        bool mask = sqDistance < sqVerletDistance;
        if(mask) {
         assert_(contactCount.count < gm.A.capacity);
         uint targetIndex = contactCount.fetchAdd(1);
         gmA[targetIndex] = a;
         gmB[targetIndex] = b;
        }
       }
      }
     }
    }
   };
   parallel_for(1, membrane->H, search);
#endif
   //if(!contactCount) continue;
   assert_(contactCount.count <= gm.A.capacity, contactCount.count, gm.A.capacity);
   gm.A.size = contactCount;
   gm.B.size = contactCount;
   gm.localAx.size = contactCount;
   gm.localAy.size = contactCount;
   gm.localAz.size = contactCount;
#if MEMBRANE_FACE
   gm.localBu.size = contactCount;
   gm.localBv.size = contactCount;
   gm.localBt.size = contactCount;
#endif

   grainMembraneRepackFrictionTime.start();
   size_t index = 0; // Index of first contact with A in old [Local]A|B list
   for(uint i=0; i<gm.A.size; i++) { // seq
    int a = gm.A[i];
    int b = gm.B[i];
    for(uint k = 0;; k++) {
     size_t j = index+k;
     if(j >= gm.oldA.size || gm.oldA[j] != a) break;
     if(gm.oldB[j] == b) { // Repack existing friction
      gm.localAx[i] = gm.oldLocalAx[j];
      gm.localAy[i] = gm.oldLocalAy[j];
      gm.localAz[i] = gm.oldLocalAz[j];
#if MEMBRANE_FACE
      gm.localBu[i] = gm.oldLocalBu[j];
      gm.localBv[i] = gm.oldLocalBv[j];
      gm.localBt[i] = gm.oldLocalBt[j];
#endif
      goto break_;
     }
    } /*else*/ { // New contact
     // Appends zero to reserve slot. Zero flags contacts for evaluation.
     // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
     gm.localAx[i] = 0;
    }
    /**/break_:;
    while(index < gm.oldA.size && gm.oldA[index] == a)
     index++;
   }
   grainMembraneRepackFrictionTime.stop();

   assert(align(simd, gm.A.size+1) <= gm.A.capacity);
   for(size_t i=gm.A.size; i<align(simd, gm.A.size +1); i++) gm.A.begin()[i] = -1;
   assert(align(simd, gm.B.size+1) <= gm.B.capacity);
   for(size_t i=gm.B.size; i<align(simd, gm.B.size +1); i++) gm.B.begin()[i] = 0;
  }
  grainMembraneGlobalMinD = verletDistance - (Grain::radius+0);
  if(grainMembraneGlobalMinD < 0) log("grainMembraneGlobalMinD", grainMembraneGlobalMinD);

  /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-membrane", grainMembraneSkipped);*/
  grainMembraneSkipped=0;
 } else grainMembraneSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 for(size_t I: range(2)) { // Face type
  auto& gm = grainMembrane[I];
  if(align(simd, gm.A.size) > gm.contacts.capacity) {
   gm.contacts = buffer<int>(align(simd, gm.A.size));
  }
  gm.contacts.size = 0;

  atomic contactCount;
  auto filter = [&](uint, uint start, uint size) {
   const int* const gmA = gm.A.data, *gmB = gm.B.data;
   const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
   const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
   float* const gmL = gm.localAx.begin();
   int* const gmContact = gm.contacts.begin();
#if MEMBRANE_POINT
   const vXsf sqRadius = floatX(sq(Grain::radius));
   for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsi A = load(gmA, i), B = load(gmB, i);
    vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
    vXsf Bx = gather(mPx, B), By = gather(mPy, B), Bz = gather(mPz, B);
    vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
    maskX contact = lessThan(sqDistance, sqRadius);
    maskStore(gmL+i, ~contact, _0f);
    uint index = contactCount.fetchAdd(countBits(contact));
    compressStore(gmContact+index, contact, intX(i)+_seqi);
   }
#else
   const float sqRadius = sq(Grain::radius);
   const int margin = membrane->margin;
   const int stride = membrane->stride;
   for(uint i=start*simd; i<(start+size)*simd; i++) { // TODO: SIMD
    const int A = gmA[i], B = gmB[i];
    const float Ax = gPx[A], Ay = gPy[A], Az = gPz[A];
    const int v = B/2;
    const float V0x = mPx[v], V0y = mPy[v], V0z = mPz[v];
    const float Ox = Ax-V0x, Oy = Ay-V0y, Oz = Az-V0z;
    const int rowIndex = (v-margin)/stride;
    const int e0 = -stride+rowIndex%2;
    const int e1 = e0-1;
    const int e2 = -1;
    if(I == 0) { // (., 0, 1) //FIXME: assert peeled
     const int e0v = v+e0;
     const int e1v = v+e1;
     const float e0x = mPx[e0v]-V0x, e0y = mPy[e0v]-V0y, e0z = mPz[e0v]-V0z;
     const float e1x = mPx[e1v]-V0x, e1y = mPy[e1v]-V0y, e1z = mPz[e1v]-V0z;
     float sqDistance;
     if(pointTriangle(Ox, Oy, Oz, e0x, e0y, e0z, e1x, e1y, e1z, sqDistance)) {
      bool mask = sqDistance < sqRadius;
      if(mask) {
       uint targetIndex = contactCount.fetchAdd(1);
       gmContact[targetIndex] = i;
      } else gmL[i] = 0;
     } else gmL[i] = 0;
    } else { // (., 1, 2)
     const int e1v = v+e1;
     const int e2v = v+e2;
     const float e1x = mPx[e1v]-V0x, e1y = mPy[e1v]-V0y, e1z = mPz[e1v]-V0z;
     const float e2x = mPx[e2v]-V0x, e2y = mPy[e2v]-V0y, e2z = mPz[e2v]-V0z;
     float sqDistance;
     if(pointTriangle(Ox, Oy, Oz, e1x, e1y, e1z, e2x, e2y, e2z, sqDistance)) {
      bool mask = sqDistance < sqRadius;
      if(mask) {
       uint targetIndex = contactCount.fetchAdd(1);
       gmContact[targetIndex] = i;
      } else gmL[i] = 0;
     } else gmL[i] = 0;
    }
   }
#endif
  };
  if(gm.A.size/simd) grainMembraneFilterTime += parallel_chunk(gm.A.size/simd, filter);
  // The partial iteration has to be executed last so that invalid contacts are trailing
  // and can be easily trimmed
  if(gm.A.size%simd != 0) filter(0, gm.A.size/simd, 1u);
  gm.contacts.size = contactCount;
  while(contactCount.count > 0 && gm.contacts[contactCount-1] >= (int)gm.A.size)
   contactCount.count--; // Trims trailing invalid contacts
  gm.contacts.size = contactCount;
  if(!gm.contacts.size) continue;
  for(size_t i=gm.contacts.size; i<align(simd, gm.contacts.size); i++)
   gm.contacts.begin()[i] = gm.A.size;

  // Evaluates forces from (packed) intersections (SoA)
  size_t GWcc = align(simd, gm.contacts.size); // Grain-Membrane contact count
  if(GWcc > gm.Fx.capacity) {
   memoryTime.start();
   gm.Fx = buffer<float>(GWcc);
   gm.Fy = buffer<float>(GWcc);
   gm.Fz = buffer<float>(GWcc);
   gm.TAx = buffer<float>(GWcc);
   gm.TAy = buffer<float>(GWcc);
   gm.TAz = buffer<float>(GWcc);
   gm.U = buffer<float>(GWcc);
   gm.V = buffer<float>(GWcc);
   memoryTime.stop();
  }
  gm.Fx.size = GWcc;
  gm.Fy.size = GWcc;
  gm.Fz.size = GWcc;
  gm.TAx.size = GWcc;
  gm.TAy.size = GWcc;
  gm.TAz.size = GWcc;
  gm.U.size = GWcc;
  gm.V.size = GWcc;
  constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Grain::poissonRatio))/Grain::elasticModulus);
  constexpr float R = 1/(Grain::curvature+Membrane::curvature);
  const float K = 4./3*E*sqrt(R);
  const float mass = 1/(1/Grain::mass+1/membrane->mass);
  const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
  grainMembraneEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainMembrane(start, size,
                          gm.contacts.data, gm.contacts.size,
                          gm.A.data, gm.B.data,
                          grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd,
                          membrane->Px.data, membrane->Py.data, membrane->Pz.data,
                        #if MEMBRANE_FACE
                          membrane->margin, membrane->stride,
                        #endif
                          Grain::radius,
                          gm.localAx.begin(), gm.localAy.begin(), gm.localAz.begin(),
                        #if MEMBRANE_FACE
                          gm.localBu.begin(), gm.localBv.begin(), gm.localBt.begin(),
                        #endif
                          floatX(K), floatX(Kb),
                          floatX(staticFrictionStiffness), floatX(dynamicGrainMembraneFrictionCoefficient),
                          floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                          grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd,
                          membrane->Vx.data, membrane->Vy.data, membrane->Vz.data,
                          grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd,
                          grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd,
                          gm.Fx.begin(), gm.Fy.begin(), gm.Fz.begin(),
                          gm.TAx.begin(), gm.TAy.begin(), gm.TAz.begin(),
                          gm.U.begin(), gm.V.begin(), I /*FIXME: assert peeled*/);
 });
  if(fail) return;
  grainMembraneContactSizeSum += gm.contacts.size;

  grainMembraneSumTime.start();
#if RADIAL
  float radialForce = 0;
#endif
#if MEMBRANE_FACE
  const int margin = membrane->margin;
  const int stride = membrane->stride;
#endif
  for(size_t i = 0; i < gm.contacts.size; i++) { // Scalar scatter add
   size_t index = gm.contacts[i];
   size_t a = gm.A[index];
   size_t b = gm.B[index];
   grain->Fx[simd+a] += gm.Fx[i];
   grain->Fy[simd+a] += gm.Fy[i];
   grain->Fz[simd+a] += gm.Fz[i];
   grain->Tx[simd+a] += gm.TAx[i];
   grain->Ty[simd+a] += gm.TAy[i];
   grain->Tz[simd+a] += gm.TAz[i];
#if RADIAL
   vec2 N = grain->position(a).xy();
   N /= length(N);
   radialForce += dot(N, vec2(gm.Fx[i], gm.Fy[i]));
#endif
#if MEMBRANE_POINT
   membrane->Fx[b] -= Fx[i];
   membrane->Fy[b] -= Fy[i];
   membrane->Fz[b] -= Fz[i];
#else
   float u = gm.U[i];
   float v = gm.V[i];
   float w = 1-u-v;
   membrane->Fx[b] -= w*gm.Fx[i];
   membrane->Fy[b] -= w*gm.Fy[i];
   membrane->Fz[b] -= w*gm.Fz[i];
   const int rowIndex = (b-margin)/stride;
   const int e0 = -stride+rowIndex%2;
   const int e1 = e0-1;
   const int e2 = -1;
   if(I == 0) { // (.,0,1)
    const int e0v = b+e0;
    const int e1v = b+e1;
    membrane->Fx[e0v] -= u*gm.Fx[i];
    membrane->Fy[e0v] -= u*gm.Fy[i];
    membrane->Fz[e0v] -= u*gm.Fz[i];
    membrane->Fx[e1v] -= v*gm.Fx[i];
    membrane->Fy[e1v] -= v*gm.Fy[i];
    membrane->Fz[e1v] -= v*gm.Fz[i];
   } else { // (.,1,2)
    const int e1v = b+e1;
    const int e2v = b+e2;
    membrane->Fx[e1v] -= u*gm.Fx[i];
    membrane->Fy[e1v] -= u*gm.Fy[i];
    membrane->Fz[e1v] -= u*gm.Fz[i];
    membrane->Fx[e2v] -= v*gm.Fx[i];
    membrane->Fy[e2v] -= v*gm.Fy[i];
    membrane->Fz[e2v] -= v*gm.Fz[i];
   }
#endif
  }
#if RADIAL
  this->radialForce += -radialForce;
 }
 radialSumStepCount++;
#endif
 grainMembraneSumTime.stop();
}
