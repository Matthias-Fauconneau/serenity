#pragma once
#include "math.h"
#include "memory.h"
#include "vector.h"
#define sconst static constexpr

constexpr size_t simd = 8; // SIMD size
inline v8sf load(ref<float> a, size_t index) { return *(v8sf*)(a.data+index); }

/// Evolving system of objects and obstacles interacting through contacts (SoA)
struct System {
 // Units
 const float dt;
 sconst float s = 1, m = 1, kg = 1, N = kg /m /(s*s), Pa = N / (m*m);
 sconst float mm = 1e-3*m, g = 1e-3*kg, MPa = 1e6 * Pa;

 // Contact parameters
 sconst float normalDamping = 1e-2 * s;
 sconst float dynamicFrictionCoefficient = 1;
 sconst float staticFrictionSpeed = inf;
 sconst float staticFrictionLength = 3 * mm; // ~ Wire::radius
 sconst float staticFrictionStiffness = 100 * 1*g*10/(1*mm); //k/F = F/L ~ Wire::mass*G/Wire::radius
 sconst float staticFrictionDamping = 1 * kg/s; // TODO: relative to k ?

 // Obstacles: floor plane, cast cylinder
 struct Obstacle {
  sconst float curvature = 0;
  sconst float elasticModulus = 1e-1 * MPa;
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
  sconst float elasticModulus = Obstacle::elasticModulus/*1e3 * MPa*/;
  sconst float angularMass = 2./3*mass*sq(radius);
  const float dt_angularMass;

  buffer<vec4> rotation { capacity }; // TODO: Rodrigues vector
  buffer<float> AVx { capacity }, AVy { capacity }, AVz { capacity }; // Angular velocity
  buffer<float> Tx { capacity }, Ty { capacity }, Tz { capacity }; // Torque

  Grain(float dt) : Mass(65536, dt, mass), dt_angularMass(dt/angularMass) {}
 } grain {dt};

 void step(Grain& p, size_t i) { // TODO: SIMD
  step((Mass&)p, i);
  p.rotation[i] += dt/2 * qmul(vec4(p.AVx[i],p.AVy[i],p.AVz[i], 0), p.rotation[i]);
  p.AVx[i] += p.dt_angularMass * p.Tx[i];
  p.AVy[i] += p.dt_angularMass * p.Ty[i];
  p.AVz[i] += p.dt_angularMass * p.Tz[i];
  p.rotation[i] *= 1./length(p.rotation[i]);
 }

 struct Wire : Mass {
  sconst float radius = 3*mm;
  sconst float internodeLength = Grain::radius/2;
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1000 * kg / cb(m);
  sconst float mass = Wire::density * Wire::volume;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1e0 * MPa;
  sconst float tensionStiffness = elasticModulus * PI * sq(radius);
  sconst float tensionDamping = mass / s;
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  sconst float bendStiffness = 0; //elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float bendDamping = 0*g /*mass*/ / s;
  Wire(float dt) : Mass(65536, dt, mass) {}
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
  v8sf kS = float8(staticFrictionStiffness) * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = float8(dynamicFrictionCoefficient) * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   if(       tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       //&& fS[k] < fD[k]
       ) {
    // Static
    if(tangentLength[k]) {
     vec3 springDirection = vec3(TOx[k], TOy[k], TOz[k]) / tangentLength[k];
     float fB = staticFrictionDamping * dot(springDirection, vec3(RVx[k], RVy[k], RVz[k]));
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0; localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0;
   }
   if(tangentRelativeSpeed[k]) {
    float fDN = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += fDN * TRVx[k];
     fTy[k] += fDN * TRVy[k];
     fTz[k] += fDN * TRVz[k];
   }
  }
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
 }

 /// Evaluates contact force between an object and an obstacle with friction (rotating A)
 // Grain - Bottom/Side
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
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    vec3 localA = qapply(conjugate(A.rotation[a[k]]), vec3(RAx[k], RAy[k], RAz[k]));
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
   }
   vec3 relativeA = qapply(A.rotation[a[k]], vec3(localAx[k], localAy[k], localAz[k]));
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
  v8sf kS = float8(staticFrictionStiffness) * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = float8(dynamicFrictionCoefficient) * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   if(      tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       //&& fS[k] < fD[k]
       ) {
    // Static
    if(tangentLength[k]) {
     vec3 springDirection = vec3(TOx[k], TOy[k], TOz[k]) / tangentLength[k];
     float fB = staticFrictionDamping * dot(springDirection, vec3(RVx[k], RVy[k], RVz[k]));
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0; localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0;
   }
   if(tangentRelativeSpeed[k]) {
    float fDN = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] += fDN * TRVx[k];
     fTy[k] += fDN * TRVy[k];
     fTz[k] += fDN * TRVz[k];
   }
  }
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
  TAx = RAy*fTz - RAz*fTy;
  TAy = RAz*fTx - RAx*fTz;
  TAz = RAx*fTy - RAy*fTx;
 }

 /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
 // Grain - Wire
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
  const v8sf fK =  float8(K) * sqrt8(depth) * depth;

  // Relative velocity
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy) - gather(B.Vx, b);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz) - gather(B.Vy, b);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx) - gather(B.Vz, b);

  // Damping
  const v8sf Kb = float8(K * normalDamping) * sqrt8(depth);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  for(size_t k: range(simd)) if(normalSpeed[k] > 0) normalSpeed[k] = 0; // Only damps penetration
  v8sf fB = - Kb * normalSpeed ; // Damping

  v8sf fN = fK+ fB;
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
    vec3 localA = qapply(conjugate(A.rotation[a[k]]), vec3(RAx[k], RAy[k], RAz[k]));
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
   }
   vec3 relativeA = qapply(A.rotation[a[k]], vec3(localAx[k], localAy[k], localAz[k]));
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
  v8sf Dx = gBx - gAx;
  v8sf Dy = gBy - gAy;
  v8sf Dz = gBz - gAz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  v8sf kS = float8(staticFrictionStiffness) * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = float8(dynamicFrictionCoefficient) * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   /*assert_(tangentLength[k] < staticFrictionLength || !isNumber(tangentLength[k]),
           tangentLength[k], staticFrictionLength);*/
   if(      tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       //&& fS[k] < fD[k]
       ) {
    // Static
    if(tangentLength[k]) {
     vec3 springDirection = vec3(TOx[k], TOy[k], TOz[k]) / tangentLength[k];
     float fB = staticFrictionDamping * dot(springDirection, vec3(RVx[k], RVy[k], RVz[k]));
     fTx[k] = (fS[k]-fB) * springDirection[0];
     fTy[k] = (fS[k]-fB) * springDirection[1];
     fTz[k] = (fS[k]-fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0; localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0;
   }
   if(tangentRelativeSpeed[k]) {
    float fDN = - fD[k] / tangentRelativeSpeed[k];
    fTx[k] += fDN * TRVx[k];
    fTy[k] += fDN * TRVy[k];
    fTz[k] += fDN * TRVz[k];
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
 // Grain - Grain
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
  Fx = NFx;
  Fy = NFy;
  Fz = NFz;

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    vec3 localA = qapply(conjugate(A.rotation[a[k]]), vec3(RAx[k], RAy[k], RAz[k]));
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
    vec3 localB = qapply(conjugate(B.rotation[b[k]]), vec3(RBx[k], RBy[k], RBz[k]));
    localBx[k] = localB[0];
    localBy[k] = localB[1];
    localBz[k] = localB[2];
   }
   vec3 relativeA = qapply(A.rotation[a[k]], vec3(localAx[k], localAy[k], localAz[k]));
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   vec3 relativeB = qapply(B.rotation[b[k]], vec3(localBx[k], localBy[k], localBz[k]));
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
  v8sf kS = float8(staticFrictionStiffness) * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = float8(dynamicFrictionCoefficient) * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   if( tangentLength[k] < staticFrictionLength
       && tangentRelativeSpeed[0] < staticFrictionSpeed
       //&& fS[k] < fD[k]
       ) {
    // Static
    if(tangentLength[k]) {
     vec3 springDirection = vec3(TOx[k], TOy[k], TOz[k]) / tangentLength[k];
     float fB = staticFrictionDamping * dot(springDirection, vec3(RVx[k], RVy[k], RVz[k]));
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
    }
   } else {
    localAx[k] = 0; localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0;
   }
   if(tangentRelativeSpeed[k]) {
    float fDN = - fD[k] / tangentRelativeSpeed[k];
    fTx[k] += fDN * TRVx[k];
    fTy[k] += fDN * TRVy[k];
    fTz[k] += fDN * TRVz[k];
   }
  }
  Fx += fTx;
  Fy += fTy;
  Fz += fTz;
  TAx = RAy*fTz - RAz*fTy;
  TAy = RAz*fTx - RAx*fTz;
  TAz = RAx*fTy - RAy*fTx;
  TBx = RBy*(-fTz) - RBz*(-fTy);
  TBy = RBz*(-fTx) - RBx*(-fTz);
  TBz = RBx*(-fTy) - RBy*(-fTx);
 }
};
