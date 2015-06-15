#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "png.h"
#include "time.h"
#include "parallel.h"
#include "gl.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
#include "algebra.h"
FILE(shader_glsl)
constexpr float pow5(float x) { return x*x*x*x*x; }

#define IMPLICIT 0
#if IMPLICIT
typedef vec<xy,double,2> vec2;
typedef vec<xyz,double,3> vec3;
typedef double real;
#else
typedef vec<xy,float,2> vec2;
typedef vec<xyz,float,3> vec3;
typedef float real;

void atomic_add(float& a, float b) {
 float expected = a;
 float desired;
 do {
  desired = expected+b;
 } while(!__atomic_compare_exchange_n((int*)&a, (int*)&expected,
                *(int*)&desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

void atomic_add(vec3& a, vec3 b) {
 atomic_add(a.x, b.x);
 atomic_add(a.y, b.y);
 atomic_add(a.z, b.z);
}

void atomic_sub(float& a, float b) {
 float expected = a;
 float desired;
 do {
  desired = expected-b;
 } while(!__atomic_compare_exchange_n((int*)&a, (int*)&expected,
                *(int*)&desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

void atomic_sub(vec3& a, vec3 b) {
 atomic_sub(a.x, b.x);
 atomic_sub(a.y, b.y);
 atomic_sub(a.z, b.z);
}
#endif

struct System {
 // (m - δt²·∂xf - δt·∂vf) δv = δt·(f + δt·∂xf·v) else m δv = δt f
#define sconst static constexpr
 sconst bool implicit = IMPLICIT;
 sconst bool rollTest = 0;
 sconst bool useWire = 1 && !rollTest;
 sconst bool useRotation = 1;
 sconst bool useFriction = 1;
 bool winchObstacle = 0 && useWire;

 // Characteristic dimensions (SI)
 sconst real T = 1; // 1 s
 sconst real L = 1; // 1 m
 sconst real M = 1; // 1 kg

 /*sconst*/ real loopAngle = 0 ? (0 ? PI*(3-sqrt(5.)) : (2*PI)/5) : 0;
 sconst real staticFrictionSpeed = (implicit?2/*1./2*/:1) * L/T; // 1
 sconst real staticFrictionFactor = (implicit?0x1p6/*5*/:0x1p5/*6*/) / L; // 5-6
 sconst real staticFrictionDamping = implicit ? 0x1p-6 : 0x1p-6; // 5-6
 sconst real frictionThreshold = 1;
 sconst real frictionFactor = 1;
 sconst real dynamicFriction = 1./16 /*F/V=M/T*/; // 16
 sconst real dampingFactor = 1./16; //1./3-64
 sconst real elasticFactor = implicit ? 1./4 : 1/*./64*/; // 32 // 1-16
 sconst real wireElasticFactor = 32; // 32-36
 sconst real tensionFactor = 1; // 1
 sconst real tensionDampingFactor = implicit ? 0x1p-8 : 0x1p-8;//4; // 4-8
 sconst real wireBendStiffness = 0;//0x1p-12; // 21,23,26,37
 sconst real wireBendDamping = 0x1p-12; // 23-25
 sconst real winchRateFactor = 128;
 sconst real winchSpeedFactor = 8; //4-16
 sconst int subStepCount = 16;
 sconst real dt = 1./(60*subStepCount*8*2*(implicit?2:2)*(rollTest?4:1));
 //sconst real dt0 = 1./(60*subStepCount*2*2*2*(implicit?1:2)*(rollTest?4:1));
 sconst real viscosity= 1 - 16*dt; // 2-32
 sconst real rotationViscosity = 1 - 128*dt; // 64-128 (~1-1/60)
 #define DBG_FRICTION 0

 HList<Plot> plots;

 struct Contact {
  vec3f relativeA, relativeB; // Relative to center but world axices
  vec3f normal;
  real depth;
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB>
 Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
  vec3f relativePosition = A.position[a] - B.position[b];
  float length = ::length(relativePosition);
  vec3f normal = relativePosition/length; // B -> A
  return {float(tA::radius)*(-normal), float(tB::radius)*(+normal),
     normal, length-tA::radius-tB::radius};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  vec3f localA, localB; // Last static friction contact location
  size_t lastUpdate = 0;
  vec3f normal = 0;
  real fN = 0;
  vec3f relativeVelocity = 0;
#if DBG_FRICTION
  bool dynamic = false;
  rgb3f color = 0;
  bool disable = false;
  array<vec3> lines = array<vec3>();
#endif
  bool operator==(const Friction& b) const {
   return index == b.index;
  }
 };

 struct Grain {
  // Properties
  sconst real radius = 2e-2; // 40 mm diameter
  sconst real thickness = radius;
  sconst real curvature = 1./radius;
  sconst real mass = 3e-3;
  sconst real elasticModulus = elasticFactor * 1e6 * M / (L*T*T);
  sconst real normalDamping = dampingFactor * M / T; // 0.3
  sconst real frictionCoefficient = frictionFactor;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;// * Grain::radius / T;
  sconst float angularMass
   = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                       / (cb(radius)-cb(radius-1e-4));

  size_t base = 0;
  sconst size_t capacity = rollTest ? 2 : 1024;
  sconst bool fixed = false;
  buffer<vec3f> position { capacity };
  buffer<vec3f> velocity { capacity };
  buffer<quat> rotation { capacity };
  buffer<vec3f> angularVelocity { capacity };
  buffer<vec3f> torque { capacity };
  real totalForce = 0;
  buffer<array<Friction>> frictions { capacity };
  //buffer<rgba4f> color { capacity };
  Grain() { frictions.clear(); /*color.clear(1);*/ }
  size_t count = 0;
 } grain;

 struct Wire {
  sconst real radius = 1e-3; // 2mm diameter
  sconst real thickness = radius;
  sconst real curvature = 1./radius;
  sconst real internodeLength = 1./2 * Grain::radius;
  sconst real volume = PI * sq(radius) * internodeLength;
  sconst real density = 1e3 * M / cb(L);
  sconst real mass = density * volume;
  sconst real angularMass = 0;
  sconst real elasticModulus = wireElasticFactor * 1e6 * M / (L*T*T); // 1e6
  sconst real normalDamping = dampingFactor * M / T; // 0.3
  sconst real frictionCoefficient = frictionFactor;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;// * Grain::radius / T;

  sconst real tensionStiffness = tensionFactor*elasticModulus*PI*sq(radius);
  sconst real tensionDamping = tensionDampingFactor * mass/T;

  size_t base = 0;
  sconst size_t capacity = useWire ? 5*1024 : 0;
  sconst bool fixed = false;
  buffer<vec3f> position { capacity };
  buffer<vec3f> velocity { capacity };
  buffer<quat> rotation { capacity }; // FIXME
  buffer<vec3f> angularVelocity { capacity }; // FIXME
  mref<vec3f> torque;
  real totalForce = 0;
  buffer<array<Friction>> frictions { capacity };
  buffer<bgr3f> color { capacity }; // FIXME
  Wire() { frictions.clear(); color.clear(1); }
  size_t count = 0;
 } wire;

 struct Floor {
  sconst real mass = inf;
  sconst real angularMass = 0;
  sconst real radius = inf;
  sconst real height = 0; //8*L/256; //Wire::radius;
  sconst real thickness = L;
  sconst real curvature = 0;
  sconst real elasticModulus = elasticFactor * 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = frictionFactor;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;// * Grain::radius / T;
  size_t base = 0; sconst size_t count = 1;
  sconst bool fixed = true;
  sconst vec3f position[1] {vec3f(0,0,0)};
  sconst vec3f velocity[1] {vec3f(0,0,0)};
  sconst vec3f angularVelocity[1] {vec3f(0,0,0)};
  sconst quat rotation[1] {quat(1,vec3f(0,0,0))};
  mref<vec3f> torque;
  real totalForce = 0;
  vec3 surfaceVelocity(size_t, vec3) const { return 0; }
 } floor;
 /// Sphere - Floor
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Floor&, size_t) {
  vec3f normal (0, 0, 1);
  return { float(tA::radius)*(-normal),
               vec3f(A.position[a].xy(), Floor::height),
               normal, A.position[a].z-tA::radius-Floor::height };
 }

 struct Side {
  sconst real mass = inf;
  sconst real angularMass = 0;
  sconst real thickness = L;
  sconst real curvature = 0; // -1/radius?
  sconst real elasticModulus = elasticFactor * 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = frictionFactor;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;// * Grain::radius / T;
  sconst bool fixed = true;
  sconst vec3f position[1] {vec3f(0,0,0)};
  sconst vec3f velocity[1] {vec3f(0,0,0)};
  sconst vec3f angularVelocity[1] {vec3f(0,0,0)};
  sconst quat rotation[1] {quat{1,vec3f(0,0,0)}};
  mref<vec3f> torque;
  real totalForce = 0;
  real castRadius = Grain::radius*10;
  size_t base = 1; sconst size_t count = 1;
  vec3 surfaceVelocity(size_t, vec3) const { return 0; }
 } side;
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Side& side, size_t) {
  vec2f r = A.position[a].xy();
  float length = ::length(r);
  vec3f normal = vec3f(-r/length, 0); // Side -> Sphere
  return { float(tA::radius)*(-normal),
               vec3f(float(side.castRadius)*-normal.xy(), A.position[a].z),
               normal, side.castRadius-tA::radius-length };
 }

 struct Load {
  sconst real mass = 1./8 * M; // kg
  sconst real angularMass = 0;
  sconst real thickness = L;
  sconst real curvature = 0;
  sconst real elasticModulus = elasticFactor * 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = frictionFactor;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;// * Grain::radius / T;
  size_t base = 0;
  sconst size_t capacity = 1;
  size_t count = 0;
  sconst bool fixed = false;
  vec3f position[1] {vec3f(0,0,0)};
  vec3f velocity[1] {vec3f(0,0,0)};
  sconst vec3f angularVelocity[1] {vec3f(0,0,0)};
  sconst quat rotation[1] {quat(1,vec3f(0,0,0))};
  mref<vec3f> torque;
  real totalForce = 0;
  vec3f surfaceVelocity(size_t, vec3f) const { return velocity[0]; }
 } load;
 /// Sphere - Load
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Load& B, size_t) {
  vec3f normal (0, 0, -1);
  return { float(tA::radius)*(-normal),
               vec3f(A.position[a].xy(), B.position[0].z),
               normal, B.position[0].z-(A.position[a].z+tA::radius) };
 }

 // Update
#if IMPLICIT
 Matrix matrix;
 buffer<vec3d> F { Grain::capacity+Wire::capacity+Load::capacity };
#else
 buffer<vec3> F { Grain::capacity+Wire::capacity+Load::capacity };
#endif
 size_t timeStep = 0;
 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 real potentialEnergy = 0;

 System() { F.clear(); }

 virtual void error(string message) abstract;
 template<Type... Args> void error(Args... args) {
  return error(str(args...));
 }

 template<Type tA> vec3f toGlobal(tA& A, size_t a, vec3f localA) {
  return A.position[a] + A.rotation[a] * localA;
 }

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB>
 void penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return;
  // Stiffness
  constexpr real E= 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr real R = 1/(tA::curvature+tB::curvature);
  //const real Ks = E*R; // TEST
  const real Ks = E*sqrt(R)*sqrt(-c.depth); // ! f(x) ~ linear approximation
  // Damping
  const real Kb = (tA::normalDamping+tB::normalDamping)/2*sqrt(-c.depth);
  //vec3 relativeVelocity = A.velocity[a] - B.velocity[b];
  vec3f relativeVelocity =
      A.velocity[a] + cross(A.angularVelocity[a], c.relativeA)
    - B.velocity[b] + cross(B.angularVelocity[b], c.relativeB);
  real fN = spring<tA,tB> //<tA::fixed, tB::fixed>
    (A.base+a, B.base+b, Ks, tA::thickness+tB::thickness+c.depth,
     tA::thickness+tB::thickness,
     Kb, c.normal //, relativeVelocity);
     , A.velocity[a], B.velocity[b]);
  B.totalForce += length(fN);
  vec3f localA = A.rotation[a].conjugate()*c.relativeA;
  vec3f localB = B.rotation[b].conjugate()*c.relativeB;
  Friction& friction = A.frictions[a].add(Friction{B.base+b, localA, localB});
  if(friction.lastUpdate < timeStep-1
#if DBG_FRICTION
  || friction.dynamic
#endif
     ) { // Resets contact
   friction.localA = localA;
   friction.localB = localB;
  }
  friction.lastUpdate = timeStep;
  friction.normal = c.normal;
  friction.fN = fN;
  friction.relativeVelocity = relativeVelocity;
 }
 template<Type tA, Type tB>
 real spring(size_t a, size_t b, real Ks, real length,
     real restLength, real Kb, vec3f normal, vec3f vA, vec3f vB) {
  real fS = - Ks * (length - restLength);
  real normalSpeed = dot(normal, vA-vB);
  real fB = - Kb * normalSpeed ; // Damping
  real fSB = fS + fB;
  potentialEnergy += 1./2 * Ks * sq(length-restLength);
#if IMPLICIT
  {
   //vec3 normalVelocity = normalSpeed*normal;
   for(int i: range(3)) {
    real f = fSB * normal[i];
    for(int j: range(i)) {
     real Nij = normal[i]*normal[j];
     real dvf = - Kb * Nij;
     real dxf = - Ks * ((1-restLength/length)*(0-Nij)+Nij);
     if(!tA::fixed) {
      matrix(a*3+i, a*3+j) += - dt*dt*dxf - dt*dvf;
      //if(!tB::fixed) matrix(a*3+i, b*3+j) -= - dt*dt*dxf - dt*dvf;
     }
     if(!tB::fixed) {
      //if(!tA::fixed) matrix(b*3+i, a*3+j) += - dt*dt*dxf - dt*dvf;
      matrix(b*3+i, b*3+j) -= - dt*dt*dxf - dt*dvf;
     }
    }
    real Nii = normal[i]*normal[i];
    real dvf = - Kb * Nii;
    real dxf = - Ks * ((1-restLength/length)*(1-Nii)+Nii);
    if(!tA::fixed) {
     matrix(a*3+i, a*3+i) += - dt*dt*dxf - dt*dvf;
     //if(!tB::fixed) matrix(a*3+i, b*3+i) -= - dt*dt*dxf - dt*dvf;
    }
    if(!tB::fixed) {
     //if(!tA::fixed) matrix(b*3+i, a*3+i) += - dt*dt*dxf - dt*dvf;
     matrix(b*3+i, b*3+i) -= - dt*dt*dxf - dt*dvf;
    }
    // normalVelocity | relativeVelocity ?
    if(!tA::fixed) F[a][i] += dt*f + dt*dt*dxf * (vA[i]-vB[i]);
    if(!tB::fixed) F[b][i] -= dt*f + dt*dt*dxf * (vA[i]-vB[i]);
    //if(!tA::fixed) F[a][i] += dt*f + dt*dt*dxf * (vA[i]-vB[i]); //relativeVelocity[i];
    //if(!tB::fixed) F[b][i] -= dt*f + dt*dt*dxf * (vA[i]-vB[i])/*vB[i]*/; //relativeVelocity[i];
    //if(!tA::fixed) F[a][i] += dt*f + dt*dt*dxf * vA[i]; //relativeVelocity[i];
    //if(!tB::fixed) F[b][i] -= dt*f + dt*dt*dxf * -/*-*/vB[i]; //? relativeVelocity[i];
   }
  }
#else
  {
   vec3 f = dt * fSB * normal;
   if(!tA::fixed) F[a] += f;
   if(!tB::fixed) atomic_sub(F[b], f);
  }
#endif
  return fS + max<real>(0, fB); // ?
 }
 template<Type tA, Type tB>
 void friction(Friction& f, tA& A, size_t a, tB& B, size_t b) {
#if DBG_FRICTION
  f.color = 0;
#endif
  if(!useFriction) return; //DEBUG
  vec3f tangentRelativeVelocity
    = f.relativeVelocity - dot(f.normal, f.relativeVelocity) * f.normal;
  float tangentRelativeSpeed = ::length(tangentRelativeVelocity);
  /*constexpr real staticFrictionThresholdSpeed
    = ::max( tA::staticFrictionThresholdSpeed,
                 tB::staticFrictionThresholdSpeed );*/
  constexpr real frictionCoefficient
    = min(tA::frictionCoefficient, tB::frictionCoefficient);
  vec3f relativeA = A.rotation[a] * f.localA;
  vec3f relativeB = B.rotation[b] * f.localB;
  vec3f globalA = A.position[a] + relativeA;
  vec3f globalB = B.position[b] +  relativeB;
  //const bool staticFriction = tangentRelativeSpeed < staticFrictionThresholdSpeed;
  //float fSB;
  vec3f fT;
  //if(staticFriction) {
   vec3f x = globalA - globalB;
   vec3f tangentOffset = x - dot(f.normal, x) * f.normal;
   float tangentLength = ::length(tangentOffset);
   //constexpr real E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
   //constexpr real R = 1/(tA::curvature+tB::curvature);
   real staticFrictionStiffness
     = staticFrictionFactor * frictionCoefficient;// * E * Grain::radius; //* R; // 1/mm
   //  1./tangentLength
   real kS = staticFrictionStiffness * f.fN;
   real fS = kS * tangentLength; // 0.1~1 fN
   vec3f springDirection = tangentLength ? tangentOffset / tangentLength : 0;
   constexpr real Kb = 0; //staticFrictionDamping * M/T/T; // Damping
   real fB = Kb * dot(springDirection, f.relativeVelocity);
   float staticFrictionForce = fS+fB;
   real dynamicFrictionForce = frictionCoefficient * f.fN;
   vec3f tangentVelocityDirection = tangentRelativeVelocity
                                                  / tangentRelativeSpeed;
   if(staticFrictionForce > dynamicFrictionForce)
    fT = - staticFrictionForce * springDirection;
   else
    fT = - dynamicFrictionForce * tangentVelocityDirection;
#if DBG_FRICTION
   f.color = rgb3f(0,1,0);
   f.dynamic = false;
#endif
  //}
#if 0
  // Dynamic friction
  if((!staticFriction || fSB > frictionThreshold*frictionCoefficient*f.fN)
     && tangentRelativeSpeed) {
#if DBG_FRICTION
   f.dynamic = true;
#else
   if(!staticFriction) f.lastUpdate = 0;
#endif
   const real dynamicFrictionCoefficient = dynamicFriction * frictionCoefficient;
   float fS = min(dynamicFrictionCoefficient * tangentRelativeSpeed,
                          frictionCoefficient * f.fN);
   // Stabilize for small speed ?
   //fS *= 1/(1+staticFrictionThresholdSpeed/tangentRelativeSpeed);
   //fS *= 1 - exp(-tangentRelativeSpeed/staticFrictionThresholdSpeed);

   constexpr real Kb = 0/*x1p-16*/ * M / T/T; // Damping
   float fB = Kb * tangentRelativeSpeed;

   fT = - (fS + fB) * tangentVelocityDirection;
   //fT = - ::min(0x1p-15, fS+fB) * tangentVelocityDirection;
   //fT = - ::min(fB, fS) * tangentVelocityDirection; // min for stability ?
#if DBG_FRICTION
    f.color = rgb3f(1,0,0);
#endif
    dynamicFrictionCount++;
  } else staticFrictionCount++;
#endif
#if IMPLICIT
  if(tangentLength)
   spring<tA, tB>(A.base+a, B.base+b, kS, tangentLength, 0, Kb,
                 springDirection, A.velocity[a], B.velocity[b]);
  //if(!tB::fixed) F[B.base+b] -= dt*vec3(fT);
#else
  if(!tA::fixed) F[A.base+a] += dt * vec3(fT);
  if(!tB::fixed) atomic_sub(F[B.base+b], dt*vec3(fT));
#endif
  // FIXME: implicit torque
  if(A.torque) A.torque[a] +=
    float(tA::angularMass / (tA::mass * sq(relativeA))) * cross(relativeA, fT);
#if IMPLICIT
  if(B.torque) B.torque[b] -=
    float(tB::angularMass / (tB::mass * sq(relativeB))) * cross(relativeB, fT);
#else
  if(B.torque)
   atomic_sub(B.torque[b],
              tB::angularMass / (tB::mass * sq(relativeB)) * cross(relativeB, fT));
#endif
 }
};
constexpr real System::viscosity;
constexpr real System::rotationViscosity;
constexpr vec3f System::Floor::position[1];
constexpr quat System::Floor::rotation[1];
constexpr vec3f System::Floor::velocity[1];
constexpr vec3f System::Floor::angularVelocity[1];
constexpr vec3f System::Side::position[1];
constexpr quat System::Side::rotation[1];
constexpr vec3f System::Side::velocity[1];
constexpr vec3f System::Side::angularVelocity[1];
constexpr quat System::Load::rotation[1];
constexpr vec3f System::Load::angularVelocity[1];

// Grid

struct Grid {
    float scale;
    int3 size;
    sconst size_t cellCapacity = 8; // Wire -> 64
    buffer<uint16> indices;
    Grid(float scale, int3 size) : scale(scale), size(size),
        indices(this->size.z*this->size.y*this->size.x*cellCapacity)
     { indices.clear(); }
    struct List : mref<uint16> {
        List(mref<uint16> o) : mref(o) {}
        size_t size() {
         size_t i = 0;
         while(at(i)!=0 && i<cellCapacity) i++;
         return i;
        }
        void remove(uint16 index) {
            size_t i = 0;
            while(at(i)!=index) i++;
            assert(i<cellCapacity);
            while(i+1<cellCapacity) { at(i) = at(i+1); i++; }
            at(i) = 0;
        }
        void append(uint16 index) { // Sorted by decreasing index
            size_t i = 0;
            while(at(i) > index) i++;
            assert(i<cellCapacity);
            while(index) { swap(index, at(i)); i++; }
            assert(i<=cellCapacity);
            if(i<cellCapacity) at(i) = 0;
        }
    };
    inline List operator[](size_t i) {
     return indices.slice(i*cellCapacity, cellCapacity);
    }
    size_t index(int x, int y, int z) {
        return (z*size[1]+y)*size[0]+x;
    }
    int3 index3(vec3f p) { // [±size/2·R, 0..size·R] -> [0..size-1]
        return int3(vec3f(size.x/2,size.y/2,0)+scale*p);
    }
    size_t index(vec3f p) {
        int3 i = index3(p);
        assert(i.x == clamp(0, i.x, size.x-1), p, i, size, scale);
        assert(i.y == clamp(0, i.y, size.y-1), p, i, size, scale);
        assert(i.z == clamp(0, i.z, size.z-1), p, i, size, scale);
        return index(i.x, i.y, i.z);
    }
};

struct Simulation : System {
 // Space partition
 Grid grainGrid {1./(2*Grain::radius), int3(16,16,16)};

#if 0
 template<Type T> bool update(/*ref<vec3d> dv*/vec3d dv, Grid& grid, T& t, size_t i) {
  t.velocity[i] += vec3(dv/*dv[t.base+i]*/);
  size_t oldCell = grid.index(t.position[i]);
  t.position[i] += dt*t.velocity[i];
  vec3 size (vec3(grainGrid.size)/grainGrid.scale);
  if(!(t.position[i].x >= -size.x/2.f && t.position[i].x < size.x/2) ||
     !(t.position[i].y >= -size.x/2.f && t.position[i].y < size.y/2) ||
     !(t.position[i].z >= 0 && t.position[i].z < size.z)) {
   grid[oldCell].remove(1+i);
   removeGrain(i);
   //error(i, "p", t.position[i], "v", t.velocity[i], "dv", dv[t.base+i]);
   /*t.position[i].x = clamp(-1., t.position[i].x, 1.f-0x1p-12);
   t.position[i].y = clamp(-1., t.position[i].y, 1.f-0x1p-12);
   t.position[i].z = clamp(0., t.position[i].z, 2.f-0x1p-12);*/
   return false;
  }
  size_t newCell = grid.index(t.position[i]);
  if(oldCell != newCell) {
   grid[oldCell].remove(1+i);
   grid[newCell].append(1+i);
  }
 }
#endif

 // Process
 const float pourRadius = side.castRadius - Wire::radius - Grain::radius;
 real pourHeight = Floor::height+Grain::radius;

 const real winchRadius = side.castRadius - 1*Grain::radius
                                                              - Wire::radius;
 sconst real winchRate = winchRateFactor / T;
 sconst real winchSpeed = winchSpeedFactor * Grain::radius / T;
 real winchRestLength = 0;
 real winchAngle = 0;

 map<rgb3f, array<vec3f>> lines;
 struct State {
  buffer<vec3> grainPositions;
  buffer<vec3> wirePositions;
 };
 array<State> states;
 int stateIndex = 0;

 real kT=0, kR=0;

 vec3f winchPosition(real winchAngle = -1) {
  if(winchAngle<0) winchAngle = this->winchAngle;
  real r = 1;
  if(loopAngle) {
    real t = mod(winchAngle / loopAngle, 4);
    /**/  if(/*0 <*/ t < 1) r = 1;
    else if(/*1 <*/ t < 2) r = 1-2*(t-1);
    else if(/*2 <*/ t < 3) r = -1;
    else    /*3 <    t < 4*/r = -1+2*(t-3);
 }
  return vec3f(winchRadius*r*cos(winchAngle),
                      winchRadius*r*sin(winchAngle),
                      pourHeight+2*Grain::radius);
 }

 Random random;
 enum { Pour, Load, Release, Done } processState = rollTest ? Done : Pour;
 int direction = 1, directionSwitchCount = 0;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainContactTime, grainFrictionTime, grainIntegrationTime;
 Time wireContactTime, wireFrictionTime, wireIntegrationTime;
 Time wireBoundTime, wireTensionTime;

 // DBG_FRICTION
 Lock lock;

 Simulation() {
  if(useWire) {
   {size_t i = addWire();
    vec3f pos = winchPosition();
    wire.position[i] = pos;
    wire.rotation[i] = quat();
    wire.velocity[i] = 0;
    wire.angularVelocity[i] = 0;
    wire.frictions.set(i);
   }
   if(winchObstacle) { // Winch obstacle
    size_t i = addGrain();
    grain.position[i] = vec3f(winchPosition().xy(),
                             winchPosition().z/*+Grain::radius-Wire::radius*/);
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    grain.rotation[i] = quat();
    grain.angularVelocity[i] = 0;
   }
  }
  if(rollTest) for(;;) {
   vec3f p(random()*2-1,random()*2-1, pourHeight);
   if(length(p.xy())>1) continue;
   vec3f newPosition (pourRadius*p.xy(), p.z); // Within cylinder
   for(vec3f p: wire.position.slice(0, wire.count))
    if(length(p - newPosition) < Grain::radius+Wire::radius)
     goto continue2_;
   for(vec3f p: grain.position.slice(0, grain.count))
    if(length(p - newPosition) < Grain::radius+Grain::radius)
     goto continue2_;
   {
    //Locker lock(this->lock);
    size_t i = addGrain();
    grain.position[i] = newPosition;
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    real t0 = 2*PI*random();
    real t1 = acos(1-2*random());
    real t2 = (PI*random()+acos(random()))/2;
    grain.rotation[i] = {float(cos(t2)), vec3f(sin(t0)*sin(t1)*sin(t2),
                         cos(t0)*sin(t1)*sin(t2),
                         cos(t1)*sin(t2))};
    grain.angularVelocity[i] = 0;
    grain.rotation[i] = quat{1, 0};
    /*if(grain.capacity == 1 && i == 0) { // Roll test
     grain.velocity[0] = vec3f((- grain.position[0] / T).xy(), 0);
     log(grain.velocity[0]);
     break;
    }
    else if(grain.capacity == 2 && i == 1) { // Collision test
     grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
     grain.velocity[1] /= ::length(grain.velocity[1]);
     break;
    }*/
   }
   continue2_:;
  }
 }

 size_t addGrain() {
  size_t index = grain.count;
  grain.count++;
  wire.base++;
  floor.base++;
  side.base++;
  load.base++;
  for(auto& frictions: grain.frictions)
   for(auto& f: frictions) if(f.index >= grain.base+index) f.index++;
  for(auto& frictions: wire.frictions)
   for(auto& f: frictions) if(f.index >= grain.base+index) f.index++;
  return index;
 }

 void removeGrain(size_t index) {
  //grainGrid[grainGrid.index(grain.position[index])].remove(1+index);
  Locker lock(this->lock);
  grain.count--;
  for(uint16& i: grainGrid.indices) if(i > index) i--;
  for(size_t i: range(index, grain.count)) {
   grain.position[i] = grain.position[i+1];
   grain.velocity[i] = grain.velocity[i+1];
   grain.rotation[i] = grain.rotation[i+1];
   grain.angularVelocity[i] = grain.angularVelocity[i+1];
   grain.torque[i] = grain.torque[i+1];
   grain.frictions[i] = ::move(grain.frictions[i+1]);
   grain.velocity[i] = grain.velocity[i+1];
  }
  wire.base--;
  floor.base--;
  side.base--;
  load.base--;
  for(auto& frictions: grain.frictions) {
   for(size_t i=0; i<frictions.size;) {
    Friction& f = frictions[i];
    if(f.index == grain.base+index) { frictions.removeAt(i); continue; }
    if(f.index > grain.base+index) f.index--;
    i++;
   }
  }
  for(auto& frictions: wire.frictions) {
   for(size_t i=0; i<frictions.size;) {
    Friction& f = frictions[i];
    if(f.index == grain.base+index) { frictions.removeAt(i); continue; }
    if(f.index > grain.base+index) f.index--;
    i++;
   }
  }
 }

 size_t addWire() {
  size_t index = wire.count;
  wire.count++;
  floor.base++;
  side.base++;
  load.base++;
  for(auto& frictions: grain.frictions)
   for(auto& f: frictions) if(f.index >= wire.base+index) f.index++;
  for(auto& frictions: wire.frictions)
   for(auto& f: frictions) if(f.index >= wire.base+index) f.index++;
  return index;
 }

 // Single implicit Euler time step
 void step() {
  stepTime.start();
  // Process
  //Locker lock(this->lock);
  if(processState == Pour &&
     pourHeight >= grainGrid.size.z/grainGrid.scale) {
   log("Max height");
   processState = Load;
  }
  if(processState == Pour) {
   // Generates falling grain (pour)
   const bool pourGrain = true;
   if(pourGrain) for(;;) {
    vec3f p(random()*2-1,random()*2-1, pourHeight);
    if(length(p.xy())>1) continue;
    vec3f newPosition (pourRadius*p.xy(), p.z); // Within cylinder
    for(vec3f p: wire.position.slice(0, wire.count))
     if(length(p - newPosition) < Grain::radius+Wire::radius)
      goto break2_;
    for(vec3f p: grain.position.slice(0, grain.count))
     if(length(p - newPosition) < Grain::radius+Grain::radius)
      goto break2_;
    Locker lock(this->lock);
    size_t i = addGrain();
    grain.position[i] = newPosition;
    grain.velocity[i] = 0;
    grain.frictions.set(i);
    grainGrid[grainGrid.index(grain.position[i])].append(1+i);
    real t0 = 2*PI*random();
    real t1 = acos(1-2*random());
    real t2 = (PI*random()+acos(random()))/2;
    grain.rotation[i] = {float(cos(t2)), vec3f( sin(t0)*sin(t1)*sin(t2),
                                                         cos(t0)*sin(t1)*sin(t2),
                                                                     cos(t1)*sin(t2))};
    grain.angularVelocity[i] = 0;
    break;
   }
   break2_:;
   pourHeight += winchSpeed * dt;
   if(useWire) { // Generates wire (winch)
    winchAngle += winchRate * dt;

    if(winchObstacle) { // Moves winch obstacle to clear spawn
     size_t i = 0;
     size_t oldCell = grainGrid.index(grain.position[i]);
     grain.position[i] = vec3f(winchPosition().xy(),
                              winchPosition().z/*+Grain::radius-Wire::radius*/);
     grain.velocity[i] = 0;
     grain.angularVelocity[0] = 0;
     size_t newCell = grainGrid.index(grain.position[i]);
     if(oldCell != newCell) {
      grainGrid[oldCell].remove(1+i);
      grainGrid[newCell].append(1+i);
     }
    }
    /*if(length(wire.position[wire.count-1]-winchPosition())
                                            >= Wire::internodeLength*(1+1./8)) {
     winchRestLength = 0;
     Locker lock(this->lock);
     size_t i = addWire();
     vec3 pos = winchPosition();
     wire.position[i] = pos;
     wire.rotation[i] = quat();
     wire.velocity[i] = 0;
     wire.angularVelocity[i] = 0;
     wire.frictions.set(i);
    }*/
    vec3f lastPosition = wire.count ? wire.position[wire.count-1] : winchPosition(0);
    vec3f r = winchPosition() - lastPosition;
    float l = length(r);
    if(l > Wire::internodeLength*(1+1./4/*2*/)) {
     Locker lock(this->lock);
     size_t i = addWire();
     vec3f pos = lastPosition + float(Wire::internodeLength)/l*r;
     wire.position[i] = pos;
     wire.rotation[i] = quat();
     wire.velocity[i] = 0;
     wire.angularVelocity[i] = 0;
     wire.frictions.set(i);
    }
   }
  }
  if(processState==Pour && (pourHeight>=2 || grain.count == grain.capacity
              || (wire.capacity && wire.count == wire.capacity))) {
   log("Poured", "Grain", grain.count, "Wire", wire.count);
   processState=Load;
  }
  if(processState == Load && !load.count) {
   Locker lock(this->lock);
   load.count++;
   float maxZ = 0;
   for(vec3f p: grain.position.slice(0, grain.count)) maxZ = ::max(maxZ, p.z);
   load.position[0] = vec3f(0,0,maxZ+Grain::radius);
   load.velocity[0] = 0;
  }
  if(processState==Release) {
   if(side.castRadius < 16*Grain::radius) {
    side.castRadius += dt * (1./16 * L/T); // Constant velocity
    // + Pressure
    float h = load.count ? load.position[0].z : pourHeight;
    float P = side.totalForce / (2*PI*side.castRadius*h);
    side.castRadius += dt * (1./16 * L/T) * P / (M*L/T/T/(L*L));
   } else {
    log("Released");
    processState = Done;
   }
  }
#if 0
  else if(winchObstacle) {
   removeGrain(0);
   /*size_t i = 0;
   grain.position[i] = vec3(winchPosition().xy(),
                                 winchPosition().z+Grain::radius-Wire::radius);
   grain.velocity[i] = 0;
   grain.angularVelocity[i] = 0;*/
   winchObstacle=0;
  }
#endif

  // Initialization
  F.size = grain.count+wire.count+load.count;
  miscTime.start();
#if IMPLICIT
  if(implicit) matrix.reset(F.size*3);
#endif
  staticFrictionCount = 0, dynamicFrictionCount = 0;
  potentialEnergy = 0;
  side.totalForce = 0;
  miscTime.stop();
  const vec3 g {0, 0, -10}; // N/kg = m·s¯²
  const uint threadCount = implicit ? 1 : ::threadCount;

  // Load
  if(load.count) {
#if IMPLICIT
   if(implicit) for(int c: range(3))
    matrix((load.base+0)*3+c, (load.base+0)*3+c) = Load::mass;
#endif
   F[load.base+0] = dt * Load::mass * g;
  }

  // Grain
  {grainTime.start();
    Locker lock(this->lock);
   // Initialization
   for(size_t i: range(grain.count)) {
    grain.torque[i] = 0;
#if IMPLICIT
    if(implicit) for(int c: range(3))
     matrix((grain.base+i)*3+c, (grain.base+i)*3+c) = Grain::mass;
#endif
    // Increased grain gravity for faster compaction ?
    F[grain.base+i] = dt * Grain::mass * g;
    //Locker lock(this->lock);
    penalty(grain, i, floor, 0);
    if(load.count)
     penalty(grain, i, load, 0);
    if(processState != Done)
     penalty(grain, i, side, 0);
   }
   // Grain - Grain repulsion
   grainContactTime.start();
   //Locker lock(this->lock);
   parallel_for(grain.count, [this](uint, int a) {
    int3 index = grainGrid.index3(grain.position[a]);
    for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z)))
     for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y)))
      for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x)))
      {
       Grid::List list = grainGrid[grainGrid.index(x, y, z)];
       for(size_t i=0; i<grainGrid.cellCapacity; i++) {
        int b = list[i]-1;
        if(b <= a) break; // Single penalty per pair, until 0
        //Locker lock(this->lock);
        penalty(grain, a, grain, b);
       }
      }
   }, threadCount);
   grainContactTime.stop();

   // Grain - Grain friction
   grainFrictionTime.start();
   if(1) {
    parallel_for(grain.count, [this](uint, size_t a) {
     for(size_t i=0; i<grain.frictions[a].size;) {
      Friction& f = grain.frictions[a][i];
      //if(f.lastUpdate >= timeStep-1) {
       if(f.lastUpdate == timeStep)  {
        size_t b = f.index;
        if(b < grain.base+grain.count) friction(f, grain, a, grain, b-grain.base);
        else if(b < wire.base+wire.count) friction(f, grain, a, wire, b-wire.base);
        else if(b==floor.base) friction(f, grain, a, floor, 0);
        else if(b==side.base) /*friction(f, grain, a, side, 0)*/;
        else if(b==load.base) friction(f, grain, a, load, 0);
        else ::error(b);
       //} // else no more contact or dynamic friction
       i++;
       //continue;
      } else
#if DBG_FRICTION
       i++;
#else
      { grain.frictions[a].removeAt(i); continue; } // Garbage collection
#endif
     }
    }, threadCount);
   }
   grainFrictionTime.stop();
   grainTime.stop();}

  // Wire
  // Initialization
  {wireTime.start();
    Locker lock(this->lock);
   wireBoundTime.start();
   for(size_t i: range(wire.count)) {
#if IMPLICIT
    if(implicit) for(int c: range(3))
     matrix((wire.base+i)*3+c, (wire.base+i)*3+c) = Wire::mass;
#endif
    F[wire.base+i] = dt * Wire::mass * g;
    //Locker lock(this->lock);
    penalty(wire, i, floor, 0);
   }
   // Winch tension
   /*if(wire.count && 0) {
    vec3 x = winchPosition()-wire.position[wire.count-1];
    if(length(x)>Wire::internodeLength/2)
     spring<false, true>(wire.count-1, 0, Wire::tensionStiffness/256, length(x),
                         winchRestLength, Wire::tensionDamping, x/length(x), 0);
   }*/
   wireBoundTime.stop();

   // Tension
   wireTensionTime.start();
   for(size_t i: range(1, wire.count)) {
    size_t a = i-1, b = i;
    vec3f relativePosition = wire.position[a] - wire.position[b];
    float length = ::length(relativePosition);
    float restLength = Wire::internodeLength;
    vec3f normal = relativePosition/length;
    //vec3 relativeVelocity = wire.velocity[a] - wire.velocity[b];
    assert(length);
    spring<Wire,Wire>(wire.base+a, wire.base+b, Wire::tensionStiffness,
                      length, restLength, Wire::tensionDamping, normal,
                      wire.velocity[a], wire.velocity[b]);
           //relativeVelocity);
   }
   wireTensionTime.stop();

   // Torsion springs (Bending resistance)
   if(wireBendStiffness) for(size_t i: range(1, wire.count-1)) {
    vec3f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
    vec3f a = C-B, b = B-A;
    vec3f c = cross(a, b);
    float l = length(c);
    if(l) {
     float p = atan(l, dot(a, b));
     vec3f dap = cross(a, cross(a,b)) / (sq(a) * l);
     vec3f dbp = cross(b, cross(b,a)) / (sq(b) * l);
     //{Locker lock(wireLock(i+1));
      F[wire.base+i+1] += dt * wireBendStiffness * vec3(-p*dap);//}
     //{Locker lock(wireLock(i));
      F[wire.base+i] += dt * wireBendStiffness * vec3(p*dap - p*dbp);//}
     //{Locker lock(wireLock(i-1));
      F[wire.base+i-1] += dt * wireBendStiffness * vec3(p*dbp);//}
      if(1) {
      vec3f A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
      vec3f axis = cross(C-B, B-A);
      if(axis) {
       float angularVelocity = atan(length(axis), dot(C-B, B-A));
       //{Locker lock(wireLock(i));
       F[wire.base+i] += dt * wireBendDamping * angularVelocity
                                     * vec3(cross(axis/length(axis), C-A) / float(2));
      //}
      }
     }
    }
   }

   wireContactTime.start();
   parallel_for(wire.count, [this](uint, int a) {
    {// Wire - Grain
     int3 index = grainGrid.index3(wire.position[a]);
     for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z)))
      for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y)))
       for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x)))
       {
        Grid::List list = grainGrid[grainGrid.index(x, y, z)];
        for(size_t i=0; i<grainGrid.cellCapacity; i++) {
         int b = list[i]-1;
         if(b<=0) break; // until 0, except grain #0 (winch obstacle)
         //Locker lock(this->lock);
         penalty(wire, a, grain, b);
        }
       }
    }
   }, threadCount);
   wireContactTime.stop();

   // Wire - Grain friction
   wireFrictionTime.start();
   if(1) {
    //Locker lock(this->lock);
   parallel_for(wire.count, [this](uint, size_t a) {
    for(size_t i=0; i<wire.frictions[a].size;) {
     Friction& f = wire.frictions[a][i];
     //if(f.lastUpdate >= timeStep-1) {
      if(f.lastUpdate == timeStep) {
       size_t b = f.index;
       if(b < grain.base+grain.count) friction(f, wire, a, grain, b-grain.base);
       else if(b < wire.base+wire.count) //friction(f, wire, a, wire, b-wire.base);
        error("ww");
       else if(b==floor.base) friction(f, wire, a, floor, 0);
       else if(b==side.base) friction(f, wire, a, side, 0);
       else if(b==load.base) friction(f, wire, a, load, 0);
       else ::error(b);
      //} // else no more contact or dynamic friction
      i++;
     } else
#if DBG_FRICTION
      i++;
#else
     { wire.frictions[a].removeAt(i); continue; } // Garbage collection
#endif
    }
   }, threadCount);}
   wireFrictionTime.stop();
   wireTime.stop();
  }

  //Locker lock(this->lock);
  solveTime.start();
  buffer<vec3> dv;
#if IMPLICIT
  if(implicit) {
   if(grain.count || wire.count) {
    matrix.factorize();
    //log(F[grain.base+1]/double(Grain::mass));
    dv = cast<vec3d>(matrix.solve(cast<double>(F)));
   }
  } else
#endif
  {
   dv = buffer<vec3>(F.capacity);
   for(size_t i: range(grain.count))
    dv[grain.base+i] = F[grain.base+i] / real(Grain::mass);
   for(size_t i: range(wire.count))
    dv[wire.base+i] = F[wire.base+i] / real(Wire::mass);
   if(load.count) dv[load.base] = F[load.base] / real(Load::mass);
  }
  solveTime.stop();

  grainTime.start();
  grainIntegrationTime.start();
  for(size_t i=0, dvI = 0; i<grain.count; dvI++) { // not range as grain.count might decrease
   //update(dv, grainGrid, grain, i);
   Grain& t = grain;
   Grid& grid = grainGrid;
    t.velocity[i] += 1.f/*/2*/*vec3f(dv[t.base+dvI]);
    t.velocity[i] *= viscosity;
    size_t oldCell = grid.index(t.position[i]);
    t.position[i] += float(dt)*t.velocity[i];
    //t.velocity[i] += 1.f/2*vec3f(dv[t.base+dvI]);
    vec3f size (vec3f(grainGrid.size)/grainGrid.scale);
    if(!(t.position[i].x >= -size.x/2.f && t.position[i].x < size.x/2) ||
       !(t.position[i].y >= -size.x/2.f && t.position[i].y < size.y/2) ||
       !(t.position[i].z >= 0 && t.position[i].z < size.z)) {
     grid[oldCell].remove(1+i);
     removeGrain(i);
     //error(i, "p", t.position[i], "v", t.velocity[i], "dv", dv[t.base+i]);
     /*t.position[i].x = clamp(-1., t.position[i].x, 1.f-0x1p-12);
     t.position[i].y = clamp(-1., t.position[i].y, 1.f-0x1p-12);
     t.position[i].z = clamp(0., t.position[i].z, 2.f-0x1p-12);*/
     continue;
    }
    size_t newCell = grid.index(t.position[i]);
    if(oldCell != newCell) {
     grid[oldCell].remove(1+i);
     grid[newCell].append(1+i);
    }
    i++;
   //}
   /*if(::length(grain.position[i].xy()) >= 14*Grain::radius) {
    removeGrain(i);
   }*/
  }

  if(useRotation) //parallel_for(grain.count, [this](uint, size_t i) {
   for(size_t i: range(grain.count)) {
#if 0
   // PCDM rotation integration
   quat& q = grain.rotation[i];
   vec3& ww = grain.angularVelocity[i];
   vec3 w = q.conjugate() * ww;
   vec3 t = q.conjugate() * grain.torque[i];
   vec3 dw = dt/Grain::angularMass * (t - cross(w, Grain::angularMass*w));
   vec3 w4 = w + 1./4*dw;
   vec3 w4w = q * w4; // -> Global
   // Prediction (multiplicative update)
   quat qp = angleVector(dt/2, w4w) * q;
   vec3 w2 = w + 1./2*dw;
   vec3 w2w = qp * w2;
   // Correction (multiplicative update)
   q = angleVector(dt, w2w) * q;
   q = normalize(q); // FIXME
   ww = q * (w + dw);
#else // Forward Euler
   vec3f& w = grain.angularVelocity[i];
   w *= rotationViscosity;
   grain.rotation[i]
     = normalize(grain.rotation[i] + dt/2 * grain.rotation[i] * quat{0, w});
   w += float(dt/Grain::angularMass) *
              (grain.torque[i] - cross(w, Grain::angularMass*w));
#endif
  }//);
  grainIntegrationTime.stop();
  grainTime.stop();

  wireTime.start();
  wireIntegrationTime.start();
  //for(size_t i: range(wire.count)) update(dv, wireGrid, wire, i);
  for(size_t i: range(wire.count)) {
   wire.velocity[i] += 1.f/*/2*/*vec3f(dv[wire.base+i]);
   wire.velocity[i] *= viscosity;
   wire.position[i] += float(dt)*wire.velocity[i];
   //wire.velocity[i] += 1.f/2*vec3f(dv[wire.base+i]);
  }
  wireIntegrationTime.stop();
  wireTime.stop();

  if(load.count) {
   load.velocity[0] += 1.f/*/2*/*vec3f(dv[load.base+0]);
   load.velocity[0] *= viscosity;
   if(direction*load.velocity[0].z >= 0) {
    direction = -direction;
    directionSwitchCount++;
    if(processState == Load) {
     log(directionSwitchCount, direction);
    }
   }
   if((directionSwitchCount>2 || kT < 1./8) && processState==Load) {
    processState=Release;
    log("release");
   }
   load.position[0] += float(dt)*load.velocity[0];
   //load.velocity[0] += 1.f/2*vec3f(dv[load.base+0]);
  }

  timeStep++;

  {
   Locker lock(this->lock);
   size_t i = 0;
   if(1) {
    if(i>=plots.size) plots.append();
    real sumV = 0, ssqV = 0;
    if(grain.count) for(vec3f v: grain.velocity.slice(0, grain.count)) {
     if(!isNumber(v)) continue; //FIXME
     assert(isNumber(v));
     sumV += length(v);
     ssqV += sq(v);
    }
    kT = 1./2*Grain::mass*ssqV;
    //plots[1].dataSets["v"__][timeStep*dt] = sumV / grain.count;
    plots[i].dataSets["Et"__][timeStep*dt] = kT;
    if(!isNumber(kT)) { log("!isNumber(kT)"); kT=0; }
    assert(isNumber(kT));
    real ssqR = 0;
    if(grain.count) for(vec3f v: grain.angularVelocity.slice(0, grain.count)) {
     if(!isNumber(v)) continue; //FIXME
     assert(isNumber(v));
     ssqR += sq(v);
    }
    kR = 1./2*Grain::angularMass*ssqR;
    if(!isNumber(kR)) { /*log("!isNumber(kR)");FIXME*/ kR=0; }
    assert(isNumber(kR));
    plots[i].dataSets["Er"__][timeStep*dt] = kR;
    //plots[i].dataSets["Ep"__][timeStep*dt] = potentialEnergy;
    i++;
   }
   if(processState < Done) {
    if(i>=plots.size) plots.append();
    float h = load.count ? load.position[0].z : pourHeight;
    plots[i].dataSets["P"__][timeStep*dt] = side.totalForce /
      (2*PI*side.castRadius*h);
    i++;
   }
  }
  stepTime.stop();
 }
};

struct SimulationView : Simulation, Widget, Poll {
 Time renderTime;

 bool stop = false;
 void error(string message) override {
  stop = true;
  log(message);
  window->setTitle(message);
 }

 void step() {
  Simulation::step();
  if(!rollTest) viewYawPitch.x += 2*PI*dt / 16;
#define THREAD 1
#if !THREAD
  if(timeStep%subStepCount == 0)
#endif
   window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 3e9) {
   /*log(//(timeStep-lastReportStep) / (elapsed*1e-9),
       "++step",str(stepTime, totalTime));*/
   if(0) { log(
       //"+misc",str(miscTime, totalTime),
       "grain",str(grainTime, totalTime),
       "wire",str(wireTime, totalTime),
       "solve",str(solveTime, totalTime));
   log(
       "grainContact",str(grainContactTime, totalTime),
       "grainFriction",str(grainFrictionTime, totalTime),
       "grainIntegration",str(grainIntegrationTime, totalTime));
   log(
       "wireContact",str(wireContactTime, totalTime),
       "wireBound",str(wireBoundTime, totalTime),
       "wireTensionTime",str(wireTensionTime, totalTime),
       "wireFriction",str(wireFrictionTime, totalTime),
       "wireIntegration",str(wireIntegrationTime, totalTime));
   }
   lastReport = realTime();
   lastReportStep = timeStep;
#if PROFILE
   requestTermination();
#endif
  }
  if(!stop) queue();
 }
 void event() {
#if PROFILE
  static unused bool once =
    ({ extern void profile_reset(); profile_reset(); true; });
#endif
  step();
 }

 unique<Window> window = ::window(this, -1);
 // View
 vec2f lastPos; // for relative cursor movements
 vec2f viewYawPitch = vec2f(0, rollTest?0:-PI/3); // Current view angles
 vec2f scale = 2./(32*Grain::radius);
 vec3f translation = 0;
 vec3f rotationCenter = 0;
#if THREAD
 Thread simulationThread;
#endif
 unique<Encoder> encoder = nullptr;
 GLFrameBuffer target {int2(1280,720)};
 bool showPlot = 0 && processState < Done;

 SimulationView()
#if THREAD
  : Poll(0, POLLIN, simulationThread)
#endif
 {
  window->actions[F12] = [this]{ writeFile(str(timeStep*dt)+".png",
                                          encodePNG(target.readback()), home()); };
  window->actions[RightArrow] = [this]{ if(stop) queue(); };
  window->actions[Space] = [this]{ stop=!stop; if(!stop) queue(); };
  window->actions[Key('p')] = [this]{ showPlot = !showPlot; };
  window->actions[Return] = [this]{
   if(processState < Done) processState++;
   log("grain", grain.count, "wire", wire.count);
  };
  /*window->actions[Key('E')] = [this] {
   if(!encoder) {
    encoder = unique<Encoder>("tas.mp4");
    encoder->setH264(window->Window::size, 60);
    encoder->open();
   }
  };*/
  if(arguments().contains("export")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

#if THREAD
  simulationThread.spawn();
#endif
  queue();
 }
 ~SimulationView() {
  log("~", "grain", grain.count, "wire", wire.count);
 }
 vec2f sizeHint(vec2f) override { return vec2f(1050, 1050*720/1280); }
 shared<Graphics> graphics(vec2f) override {
  renderTime.start();

  const real dt = 1./60;
  {
   vec3f min = 0, max = 0;
   //Locker lock(this->lock);
   for(size_t i: range(grain.count)) { // FIXME: proper BS
    min = ::min(min, vec3f(grain.position[i]));
    max = ::max(max, vec3f(grain.position[i]));
   }
   for(size_t i: range(wire.count)) { // FIXME: proper BS
    min = ::min(min, vec3f(wire.position[i]));
    max = ::max(max, vec3f(wire.position[i]));
   }
   vec3f rotationCenter = (min+max)/float(2);
   rotationCenter.xy() = 0;
   if(!rollTest)
    this->rotationCenter = this->rotationCenter*float(1-dt)
                                       + float(dt)*rotationCenter;
    //this->rotationCenter = rotationCenter;
  }

  quat viewRotation = angleVector(viewYawPitch.y, vec3f(1,0,0)) *
                                 angleVector(viewYawPitch.x, vec3f(0,0,1));

  vec3f min = -1./32, max = 1./32;
  {//Locker lock(this->lock);
   for(size_t i: range(grain.count)) {
    vec3f O = viewRotation * vec3f(grain.position[i])-rotationCenter;
    min = ::min(min, O - vec3f(vec2f(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3f(vec2f(Grain::radius), 0)); // Parallel
   }
   for(size_t i: range(wire.count)) {
    vec3f O = viewRotation * vec3f(wire.position[i])-rotationCenter;
    min = ::min(min, O - vec3f(vec2f(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3f(vec2f(Grain::radius), 0)); // Parallel
   }
  }

  vec2f size (target.size.x, target.size.y);
  vec2f viewSize = size;
  if(showPlot) {
   viewSize = vec2f(720, 720);
   if(plots.size>2) viewSize = vec2f(360,720);
  }

  vec3f scale (2*::min(viewSize/(max-min).xy())/size,
                    -1/(2*(max-min).z));
  scale.xy() = this->scale = this->scale*float(1-dt) + float(dt)*scale.xy();

  //if(rollTest) scale.xy() = vec2f(viewSize.x/(16*Grain::radius))/viewSize;
  vec3f fitTranslation = -scale*(min+max)/float(2);
  //vec2 aspectRatio (size.x/size.y, 1);
  vec3f translation = this->translation = vec3f((size-viewSize)/size, 0);
  //this->translation*(1-dt) + dt*fitTranslation;
  /*mat4 viewProjection;
  for(int e: range(3)) {
   vec3 axis = 0; axis[e] = 1;
   viewProjection[e] = vec4(scale*(viewRotation*axis),
                            (translation + scale*(viewRotation*(-rotationCenter)))[e]);
  }*/
  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-rotationCenter);

  //map<rgb3f, array<vec3>> lines;
  lines.clear();

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);


  {Locker lock(this->lock);
  if(grain.count) {
   buffer<float3> positions {grain.count*6};
   for(size_t i: range(grain.count)) {
    // FIXME: GPU quad projection
    vec3f O = viewProjection * vec3f(grain.position[i]);
    vec2f min = O.xy() - vec2f(scale.xy()) * vec2f(Grain::radius); // Parallel
    vec2f max = O.xy() + vec2f(scale.xy()) * vec2f(Grain::radius); // Parallel
    positions[i*6+0] = float3(min, O.z);
    positions[i*6+1] = float3(max.x, min.y, O.z);
    positions[i*6+2] = float3(min.x, max.y, O.z);
    positions[i*6+3] = float3(min.x, max.y, O.z);
    positions[i*6+4] = float3(max.x, min.y, O.z);
    positions[i*6+5] = float3(max, O.z);

    for(int d: range(0)) {
     vec3f axis = 0; axis[d] = 1;
     lines[rgb3f(vec3f(axis))].append(viewProjection*vec3f(toGlobal(grain, i, 0)));
     lines[rgb3f(vec3f(axis))].append(viewProjection*vec3f(toGlobal(grain, i,
                                                       float(Grain::radius/2)*axis)));
    }
#if DBG_FRICTION && 0
    for(const Friction& f : grain.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3 A = toGlobal(grain, i, f.localA);
     size_t b = f.index;
     vec3 B =
       b < grain.base+grain.count ? toGlobal(grain, b-grain.base, f.localB) :
       b < wire.base+wire.count ? toGlobal(wire, b-wire.base, f.localB) :
       b == floor.base ? toGlobal(floor, b-floor.base, f.localB) :
       b==side.base ? toGlobal(side, b-side.base, f.localB) :
       (::error("grain", b), vec3());
     vec3f vA = viewProjection*vec3f(A), vB=viewProjection*vec3f(B);
     if(length(vA-vB) < 16/size.y) vA.y -= 8/size.y, vB.y += 8/size.y;
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   assert_(grain.count);
   GLBuffer rotationBuffer (apply(grain.rotation.slice(0, grain.count),
                [=](quat q) -> quat { return (viewRotation*q).conjugate(); }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   /*GLBuffer colorBuffer (grain.color.slice(start));
   shader.bind("colorBuffer"_, colorBuffer, 1);*/
   shader["radius"] = float(scale.z/2 * Grain::radius);
   //glBlendAlpha();
   vertexArray.draw(Triangles, positions.size);
  }}

  {Locker lock(this->lock);
  if(wire.count>1) {
   size_t wireCount = wire.count;
   buffer<float3> positions {(wireCount-1)*6};
   for(size_t i: range(wireCount-1)) {
    float3 a (wire.position[i]), b (wire.position[i+1]);
    // FIXME: GPU quad projection
    float3 A = viewProjection * a, B= viewProjection * b;
    float2 r = B.xy()-A.xy();
    float l = length(r);
    float2 t = r/l;
    float3 n = float3(scale)*float(Wire::radius)*float3(t.y, -t.x, 0);
    float3 P[4] {A-n, A+n, B-n, B+n};
    positions[i*6+0] = P[0];
    positions[i*6+1] = P[1];
    positions[i*6+2] = P[2];
    positions[i*6+3] = P[2];
    positions[i*6+4] = P[1];
    positions[i*6+5] = P[3];

#if DBG_FRICTION
    for(const Friction& f : wire.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3f A = toGlobal(wire, i, f.localA);
     size_t b = f.index;
     if(b >= grain.base+grain.count) continue;
     vec3f B =
      b < grain.base+grain.count ? toGlobal(grain, b-grain.base, f.localB) :
      b < wire.base+wire.count ? /*toGlobal(wire, b-wire.base, f.localB)*/
                               (::error(wire.base,b,wire.count), vec3f()) :
      b==floor.base ? toGlobal(floor, b-floor.base, f.localB) :
      b==side.base ? toGlobal(side, b-side.base, f.localB) :
      (::error("wire", b), vec3f());
     vec3f vA = viewProjection*vec3f(A), vB=viewProjection*vec3f(B);
     if(length(vA-vB) < 2/size.y) {
      if(vA.y<vB.y) vA.y -= 4/size.y, vB.y += 4/size.y;
      else vA.y += 4/size.y, vB.y -= 4/size.y;
     }
     //log(f.color, (vA+vec3f(1.f))/2.f*float(size.x), (vB+vec3f(1.f))/2.f*float(size.y));
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
   }
   static GLShader shader {::shader_glsl(), {"cylinder"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["radius"] = float(scale.z/2 * Wire::radius);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }}

  if(load.count) {
   //Locker lock(this->lock);
   vec3 min (-vec2(-side.castRadius), load.position[0].z);
   vec3 max (+vec2(-side.castRadius), load.position[0].z);
   for(int i: range(0b111 +1)) for(int j: {(i&0b110) |0b001, (i&0b101) |0b010, (i&0b011) |0b100}) {
    if(i<j) {
     auto p = [=](int i) {
      return vec3f(i&0b001?max[0]:min[0], i&0b010?max[1]:min[1], i&0b100?max[2]:min[2]);
      };
      lines[rgb3f(1,0,0)].append(viewProjection*p(i));
      lines[rgb3f(1,0,0)].append(viewProjection*p(j));
     }
    }
  }

  glDepthTest(false);
  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});
  static GLVertexArray vertexArray;
  for(auto entry: lines) {
   shader["uColor"] = entry.key;
   GLBuffer positionBuffer (entry.value);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Lines, entry.value.size);
  }
  lines.clear();

  if(plots) {
   Image target(int2(size.x-viewSize.x, size.y), true);
   target.clear(byte4(byte3(0xFF), 0));
   this->lock.lock();
   auto graphics = plots.graphics(vec2f(target.size), Rect(vec2f(target.size)));
   this->lock.unlock();
   render(target, graphics);
   GLTexture image = flip(move(target));
   static GLShader shader {::shader_glsl(), {"blit"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   shader["image"] = 0; image.bind(0);
   vec2f min (-1,-1), max (vec2f(target.size)/size*float(2)-vec2f(1));
   GLBuffer positionBuffer (ref<vec2f>{
                  vec2f(min.x,min.y), vec2f(max.x,min.y), vec2f(min.x,max.y),
                  vec2f(min.x,max.y), vec2f(max.x,min.y), vec2f(max.x,max.y)});
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             2, Float, positionBuffer);
   glBlendAlpha();
   vertexArray.draw(Triangles, 6);
  }

  if(encoder) {
   encoder->writeVideoFrame(target.readback());
   if(stop) encoder = nullptr;
  }
  target.blit(0, window->size);
  renderTime.stop();
  if(stop && fitTranslation != this->translation) window->render();

  array<char> s = str(timeStep*dt, grain.count, wire.count/*, kT, kR*/
                      /*,staticFrictionCount, dynamicFrictionCount*/);
  if(load.count) s.append(" "_+str(load.position[0].z));
  window->setTitle(s);
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2f cursor, vec2f size, Event event, Button button,
                                Widget*&) override {
  vec2f delta = cursor-lastPos; lastPos=cursor;
  if(event==Press && button==WheelDown) {
   stateIndex = clamp(0, stateIndex-1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }
  if(event==Press && button==WheelUp) {
   stateIndex = clamp(0, stateIndex+1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
   if(encoder) encoder = nullptr;
  }
  else return false;
  return true;
 }
} view;
