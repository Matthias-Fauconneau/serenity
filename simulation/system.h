#pragma once
#include "math.h"
#include "memory.h"
#include "vector.h"
#define sconst static constexpr

constexpr size_t simd = 8; // SIMD size

/// Evolving system of objects and obstacles interacting through contacts (SoA)
struct System {
 // Units
 const float dt;
 sconst float s = 1, m = 1, kg = 1, N = kg /m /(s*s), Pa = N / (m*m);
 sconst float mm = 1e-3*m, g = 1e-3*kg, MPa = 1e6 * Pa;
 sconst float Gz = -0/*10*/ * N/kg; // Gravity

 // Contact parameters
 sconst float normalDamping = 100e-6 * s; // ~dt
 sconst float staticFrictionSpeed = inf;
 sconst float staticFrictionFactor = 1;
 sconst float staticFrictionLength = 10 * mm;
 sconst float staticFrictionDamping = 0/*2.7*/ * g/s; // TODO: relative to k ?
 sconst float staticFrictionCoefficient = 0.3;
 sconst float dynamicFrictionCoefficient = 0.3;

 // Obstacles: floor plane, cast cylinder
 struct Obstacle {
  sconst float curvature = 0;
  sconst float elasticModulus = 1 *MPa;
 };
 float radius; // Cylinder radius

 // Mass points/vertices/nodes (SoA)
 struct Mass {
  const size_t capacity;
  size_t count = 0;
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
  buffer<float> Vx { capacity };
  buffer<float> Vy { capacity };
  buffer<float> Vz { capacity };
  buffer<float> Fx { capacity };
  buffer<float> Fy { capacity };
  buffer<float> Fz { capacity };
  const float dt_mass;

  Mass(size_t capacity, float dt, float mass)
   : capacity(capacity), dt_mass(dt / mass) {}

  const vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]);  }
  const vec3 velocity(size_t i) const { return vec3(Vx[i], Vy[i], Vz[i]);  }
  const vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]);  }
 };

 void step(Mass& p, size_t i) { // TODO: SIMD
  assert_(isNumber(p.Fx[i]));
  p.Vx[i] += p.dt_mass * p.Fx[i];
  p.Vy[i] += p.dt_mass * p.Fy[i];
  p.Vz[i] += p.dt_mass * p.Fz[i];
  p.Px[i] += dt * p.Vx[i];
  p.Py[i] += dt * p.Vy[i];
  p.Pz[i] += dt * p.Vz[i];
 }

 // Sphere particles
 struct Grain : Mass {
  sconst float mass = 2.7 * g;
  sconst float radius = 40 *mm;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1 * MPa;
  sconst float angularMass = 2./3*mass*sq(radius);
  const float dt_angularMass;

  buffer<v4sf> rotation { capacity }; // TODO: Rodrigues vector
  buffer<float> AVx { capacity }, AVy { capacity }, AVz { capacity }; // Angular velocity
  buffer<float> Tx { capacity }, Ty { capacity }, Tz { capacity }; // Torque

  Grain(float dt) : Mass(256, dt, mass), dt_angularMass(dt/angularMass) {}
 } grain {dt};

 void step(Grain& p, size_t i) { // TODO: SIMD
  step((Mass&)p, i);
  p.rotation[i] += float4(dt/2) * qmul((v4sf){p.AVx[i],p.AVy[i],p.AVz[i],0}, p.rotation[i]);
  p.AVx[i] += p.dt_angularMass * p.Tx[i];
  p.AVy[i] += p.dt_angularMass * p.Ty[i];
  p.AVz[i] += p.dt_angularMass * p.Tz[i];
  p.rotation[i] *= rsqrt(sq4(p.rotation[i]));
 }

 struct Wire : Mass {
  sconst float radius = 3*mm;
  sconst float internodeLength = Grain::radius;
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1e3 * kg / cb(m);
  sconst float mass = Wire::density * Wire::volume;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 2e6;
  sconst float tensionStiffness = elasticModulus * PI * sq(radius);
  sconst float tensionDamping = mass / s;
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  sconst float bendStiffness = elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float bendDamping = mass / s;
  Wire(float dt) : Mass(256, dt, mass) {}
 } wire {dt};

 size_t timeStep = 0;

 System(float dt) : dt(dt) {}

 /// Evaluates contact force between an object and an obstacle with friction (non-rotating A)
 // Wire - Floor/Side
 template<Type tA, Type tB> inline void contact(
   const tA& A, v8ui a,
   v8sf depth,
   v8sf RAx, v8sf RAy, v8sf RAz,
   v8sf Nx, v8sf Ny, v8sf Nz,
   v8sf Ax, v8sf Ay, v8sf Az,
   v8sf& localAx, v8sf& localAy, v8sf& localAz,
   v8sf& localBx, v8sf& localBy, v8sf& localBz,
   v8sf& Fx, v8sf& Fy, v8sf& Fz) {

  // Tension
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R);
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Relative velocity
  v8sf RVx = gather(A.Vx, a);
  v8sf RVy = gather(A.Vy, a);
  v8sf RVz = gather(A.Vz, a);

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ; // Damping

  v8sf fN = fK + fB;
  v8sf NFx = fN * Nx;
  v8sf NFy = fN * Ny;
  v8sf NFz = fN * Nz;
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    localAx[k] = RAx[k];
    localAy[k] = RAy[k];
    localAz[k] = RAz[k];
   }
   FRAx[k] = localAx[k];
   FRAy[k] = localAy[k];
   FRAz[k] = localAz[k];
   FRBx[k] = localBx[k];
   FRBy[k] = localBy[k];
   FRBz[k] = localBz[k];
  }

  v8sf gAx = Ax + FRAx;
  v8sf gAy = Ay + FRAy;
  v8sf gAz = Az + FRAz;
  v8sf gBx = FRBx;
  v8sf gBy = FRBy;
  v8sf gBz = FRBz;
  v8sf Dx = gAx - gBx;
  v8sf Dy = gAy - gBy;
  v8sf Dz = gAz - gBz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  sconst v8sf staticFrictionStiffness = float8(staticFrictionFactor * staticFrictionCoefficient);
  v8sf kS = staticFrictionStiffness * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  sconst v8sf dynamicFrictionCoefficient8 = float8(dynamicFrictionCoefficient);
  v8sf fD = dynamicFrictionCoefficient8 * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   // Wire - Grain
   if( 1/*tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       && fS[k] < fD[k]*/
       ) {
    // Static
    if(tangentLength[k]) {
     v4sf springDirection = v4sf{TOx[k], TOy[k], TOz[k], 0} / float4(tangentLength[k]);
     float fB = staticFrictionDamping * dot3(springDirection, v4sf{RVx[k], RVy[k], RVz[k], 0})[0];
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0;
    localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0; // DEBUG
   }
   if(/*tangentRelativeSpeed[k]*/ 1) {
    float scale = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += scale * TRVx[k];
     fTy[k] += scale * TRVy[k];
     fTz[k] += scale * TRVz[k];
   }
  }
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
 }

 /// Evaluates contact force between an object and an obstacle with friction (rotating A)
 // Grain - Floor/Side
 template<Type tA, Type tB> inline void contact(
   const tA& A, v8ui a,
   v8sf depth,
   v8sf RAx, v8sf RAy, v8sf RAz,
   v8sf Nx, v8sf Ny, v8sf Nz,
   v8sf Ax, v8sf Ay, v8sf Az,
   v8sf& localAx, v8sf& localAy, v8sf& localAz,
   v8sf& localBx, v8sf& localBy, v8sf& localBz,
   v8sf& Fx, v8sf& Fy, v8sf& Fz,
   v8sf& TAx, v8sf& TAy, v8sf& TAz
   ) {

  // Tension
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R);
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Relative velocity
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx);

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ; // Damping

  v8sf fN = fK + fB;
  v8sf NFx = fN * Nx;
  v8sf NFy = fN * Ny;
  v8sf NFz = fN * Nz;
  for(size_t k: range(simd)) assert_(isNumber(NFx[k]), k);
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    v4sf localA = qapply(conjugate(A.rotation[a[k]]), (v4sf){RAx[k], RAy[k], RAz[k], 0});
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
   }
   v4sf relativeA = qapply(A.rotation[a[k]], (v4sf){localAx[k], localAy[k], localAz[k], 0});
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   FRBx[k] = localBx[k];
   FRBy[k] = localBy[k];
   FRBz[k] = localBz[k];
  }

  v8sf gAx = Ax + FRAx;
  v8sf gAy = Ay + FRAy;
  v8sf gAz = Az + FRAz;
  v8sf gBx = FRBx;
  v8sf gBy = FRBy;
  v8sf gBz = FRBz;
  v8sf Dx = gAx - gBx;
  v8sf Dy = gAy - gBy;
  v8sf Dz = gAz - gBz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  sconst v8sf staticFrictionStiffness = float8(staticFrictionFactor * staticFrictionCoefficient);
  v8sf kS = staticFrictionStiffness * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  sconst v8sf dynamicFrictionCoefficient8 = float8(dynamicFrictionCoefficient);
  v8sf fD = dynamicFrictionCoefficient8 * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   // Wire - Grain
   if( 1/*tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       && fS[k] < fD[k]*/
       ) {
    // Static
    if(tangentLength[k]) {
     v4sf springDirection = v4sf{TOx[k], TOy[k], TOz[k], 0} / float4(tangentLength[k]);
     float fB = staticFrictionDamping * dot3(springDirection, v4sf{RVx[k], RVy[k], RVz[k], 0})[0];
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0;
    localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0; // DEBUG
   }
   if(/*tangentRelativeSpeed[k]*/ 1) {
    float scale = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += scale * TRVx[k];
     fTy[k] += scale * TRVy[k];
     fTz[k] += scale * TRVz[k];
   }
  }
  for(size_t k: range(simd)) assert_(isNumber(fTx[k]), k);
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
  TAx = RAy*fTz - RAz*fTy;
  TAy = RAz*fTx - RAx*fTz;
  TAz = RAx*fTy - RAy*fTx;
 }

 /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
 template<Type tA, Type tB> inline void contact(
   const tA& A, v8ui a,
   tB& B, v8ui b,
   v8sf depth,
   v8sf RAx, v8sf RAy, v8sf RAz,
   v8sf Nx, v8sf Ny, v8sf Nz,
   v8sf Ax, v8sf Ay, v8sf Az,
   v8sf Bx, v8sf By, v8sf Bz,
   v8sf& localAx, v8sf& localAy, v8sf& localAz,
   v8sf& localBx, v8sf& localBy, v8sf& localBz,
   v8sf& Fx, v8sf& Fy, v8sf& Fz,
   v8sf& TAx, v8sf& TAy, v8sf& TAz
   ) {

  // Tension
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R);
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Relative velocity
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy) - gather(B.Vx, b);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz) - gather(B.Vy, b);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx) - gather(B.Vz, b);

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ; // Damping

  v8sf fN = fK + fB;
  v8sf NFx = fN * Nx;
  v8sf NFy = fN * Ny;
  v8sf NFz = fN * Nz;
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    v4sf localA = qapply(conjugate(A.rotation[a[k]]), (v4sf){RAx[k], RAy[k], RAz[k], 0});
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
   }
   v4sf relativeA = qapply(A.rotation[a[k]], (v4sf){localAx[k], localAy[k], localAz[k], 0});
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   FRBx[k] = localBx[k];
   FRBy[k] = localBy[k];
   FRBz[k] = localBz[k];
  }

  v8sf gAx = Ax + FRAx;
  v8sf gAy = Ay + FRAy;
  v8sf gAz = Az + FRAz;
  v8sf gBx = Bx + FRBx;
  v8sf gBy = By + FRBy;
  v8sf gBz = Bz + FRBz;
  v8sf Dx = gAx - gBx;
  v8sf Dy = gAy - gBy;
  v8sf Dz = gAz - gBz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  sconst v8sf staticFrictionStiffness = float8(staticFrictionFactor * staticFrictionCoefficient);
  v8sf kS = staticFrictionStiffness * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN
  //for(size_t k: range(simd)) log(fS[k]);

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  sconst v8sf dynamicFrictionCoefficient8 = float8(dynamicFrictionCoefficient);
  v8sf fD = dynamicFrictionCoefficient8 * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   // Wire - Grain
   if( 1/*tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       && fS[k] < fD[k]*/
       ) {
    // Static
    if(tangentLength[k]) {
     v4sf springDirection = v4sf{TOx[k], TOy[k], TOz[k], 0} / float4(tangentLength[k]);
     float fB = staticFrictionDamping * dot3(springDirection, v4sf{RVx[k], RVy[k], RVz[k], 0})[0];
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0;
    localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0; // DEBUG
   }
   if(/*tangentRelativeSpeed[k]*/ 1) {
    float scale = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += scale * TRVx[k];
     fTy[k] += scale * TRVy[k];
     fTz[k] += scale * TRVz[k];
   }
  }
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
  TAx = RAy*fTz - RAz*fTy;
  TAy = RAz*fTx - RAx*fTz;
  TAz = RAx*fTy - RAy*fTx;
 }

 /// Evaluates contact force between two objects with friction (rotating A, rotating B)
 template<Type tA, Type tB> inline void contact(
   const tA& A, v8ui a,
   tB& B, v8ui b,
   v8sf depth,
   v8sf RAx, v8sf RAy, v8sf RAz,
   v8sf RBx, v8sf RBy, v8sf RBz,
   v8sf Nx, v8sf Ny, v8sf Nz,
   v8sf Ax, v8sf Ay, v8sf Az,
   v8sf Bx, v8sf By, v8sf Bz,
   v8sf& localAx, v8sf& localAy, v8sf& localAz,
   v8sf& localBx, v8sf& localBy, v8sf& localBz,
   v8sf& Fx, v8sf& Fy, v8sf& Fz,
   v8sf& TAx, v8sf& TAy, v8sf& TAz,
   v8sf& TBx, v8sf& TBy, v8sf& TBz
   ) {

  // Tension
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R);
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Relative velocity
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf BVx = gather(B.AVx, b), BVy = gather(B.AVy, b), BVz = gather(B.AVz, b);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy) - gather(B.Vx, b) - (BVy*RBz - BVz*RBy);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz) - gather(B.Vy, b) - (BVz*RBx - BVx*RBz);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx) - gather(B.Vz, b) - (BVx*RBy - BVy*RBx);

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ; // Damping

  v8sf fN = fK + fB;
  v8sf NFx = fN * Nx;
  v8sf NFy = fN * Ny;
  v8sf NFz = fN * Nz;
  for(size_t k: range(simd)) assert_(isNumber(NFx[k]), k);
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    v4sf localA = qapply(conjugate(A.rotation[a[k]]), (v4sf){RAx[k], RAy[k], RAz[k], 0});
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
    v4sf localB = qapply(conjugate(B.rotation[b[k]]), (v4sf){RBx[k], RBy[k], RBz[k], 0});
    localBx[k] = localB[0];
    localBy[k] = localB[1];
    localBz[k] = localB[2];
   }
   v4sf relativeA = qapply(A.rotation[a[k]], (v4sf){localAx[k], localAy[k], localAz[k], 0});
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   v4sf relativeB = qapply(B.rotation[b[k]], (v4sf){localBx[k], localBy[k], localBz[k], 0});
   FRBx[k] = relativeB[0];
   FRBy[k] = relativeB[1];
   FRBz[k] = relativeB[2];
  }

  v8sf gAx = Ax + FRAx;
  v8sf gAy = Ay + FRAy;
  v8sf gAz = Az + FRAz;
  v8sf gBx = Bx + FRBx;
  v8sf gBy = By + FRBy;
  v8sf gBz = Bz + FRBz;
  v8sf Dx = gAx - gBx;
  v8sf Dy = gAy - gBy;
  v8sf Dz = gAz - gBz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  sconst v8sf staticFrictionStiffness = float8(staticFrictionFactor * staticFrictionCoefficient);
  v8sf kS = staticFrictionStiffness * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  sconst v8sf dynamicFrictionCoefficient8 = float8(dynamicFrictionCoefficient);
  v8sf fD = dynamicFrictionCoefficient8 * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   if( tangentLength[k] < staticFrictionLength
       //&& tangentRelativeSpeed[0] < staticFrictionSpeed
       //&& fS[k] < fD[k]
       ) {
    // Static
    if(tangentLength[k]) {
     v4sf springDirection = v4sf{TOx[k], TOy[k], TOz[k], 0} / float4(tangentLength[k]);
     float fB = staticFrictionDamping * dot3(springDirection, v4sf{RVx[k], RVy[k], RVz[k], 0})[0];
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else {
    localAx[k] = 0;
    localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0; // DEBUG
   }
   if(tangentRelativeSpeed[k]) {
    float scale = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += scale * TRVx[k];
     fTy[k] += scale * TRVy[k];
     fTz[k] += scale * TRVz[k];
   }
  }
  for(size_t k: range(simd)) assert_(isNumber(fTx[k]), k);
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
  TAx = RAy*fTz - RAz*fTy;
  TAy = RAz*fTx - RAx*fTz;
  TAz = RAx*fTy - RAy*fTx;
  TBx = RBy*fTz - RBz*fTy;
  TBy = RBz*fTx - RBx*fTz;
  TBz = RBx*fTy - RBy*fTx;
 }
};
