#pragma once
#include "simd.h"
#include "parallel.h"
#include "variant.h"
#include "matrix.h"
typedef v4sf vec4f;
//typedef v8sf vec8f;
inline float length2(vec4f v) { return sqrt(sq2(v))[0]; }
inline float length(vec4f v) { return sqrt(sq3(v))[0]; }

#define sconst static constexpr

struct NoOperation {};
inline void operator +=(NoOperation, float) {}
inline void operator -=(NoOperation, float) {}

struct NoOperation4 { float operator [](size_t) const { return 0; } };
inline void operator +=(NoOperation4, v4sf) {}
inline void atomic_add(NoOperation4, v4sf) {}
inline void atomic_sub(NoOperation4, v4sf) {}

struct RadialSum {
 float radialSum;
 RadialSum& operator[](size_t){ return *this; }
 void operator -=(vec4f f) { radialSum += length2(f); }
};
inline void atomic_sub(RadialSum& t, vec4f f) { t.radialSum += length2(f); }

sconst bool validation = true;

struct System {
 // Integration
 const float dt;
 const vec4f b[4] = {float3(dt), float3(dt*dt/2), float3(dt*dt*dt/6), float3(dt*dt*dt*dt/24)};
 const vec4f c[5] = {float3(19./90), float3(3/(4*dt)), float3(2/(dt*dt)), float3(6/(2*dt*dt*dt)),
                                float3(24/(12*dt*dt*dt*dt))};

 // SI units
 sconst float s = 1, m = 1, kg = 1, N = kg*m/s/s;
 sconst float mm = 1e-3*m, g = 1e-3*kg;
 const float gz;
 sconst float densityScale = 5e4; // 5-6
 vec3 G {0, 0, -gz/densityScale}; // Scaled gravity

 // Penalty model
 sconst float normalDamping = 0.05;
 // Friction model
 sconst float staticFrictionSpeed = inf; //1e-1 *m/s; // inf
 sconst float staticFrictionFactor = 1e3;// 5e3-1e5
 sconst float staticFrictionLength = 3e-4 * m;
 sconst float staticFrictionDamping = 15 *g/s;
 sconst float frictionCoefficient = 0.3;
 sconst v8sf frictionCoefficient8 = float8(0.3);

 struct Vertex {
  const size_t base, capacity;
  float mass = 0;
  vec4f _1_mass = float4(0);
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
  buffer<float> Vx { capacity };
  buffer<float> Vy { capacity };
  buffer<float> Vz { capacity };
#define GEAR 0
#define EULER !GEAR
#if GEAR
  buffer<float> PDx[3], PDy[3], PDz[3]; // Position derivatives
#endif
  buffer<float> Fx { capacity };
  buffer<float> Fy { capacity };
  buffer<float> Fz { capacity };
  size_t count = 0;

  struct {
   const Vertex& this_;
   const vec3 operator[](size_t i) const { return vec3(this_.Px[i], this_.Py[i], this_.Pz[i]); }
  } position {*this};
  struct {
   const Vertex& this_;
   const vec3 operator[](size_t i) const { return vec3(this_.Vx[i], this_.Vy[i], this_.Vz[i]); }
  } velocity {*this};

  //struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
  //struct { vec4f operator[](size_t) const { return _0f4; }} angularVelocity;

  Vertex(size_t base, size_t capacity, float mass) : base(base), capacity(capacity),
    mass(mass), _1_mass(float3(1./mass)) {
#if GEAR
   for(size_t i: range(3)) {
    PDx[i] = buffer<float>(capacity);
    PDy[i] = buffer<float>(capacity);
    PDz[i] = buffer<float>(capacity);
   }
#endif
  }
 };

 void step(Vertex& p, size_t i) { // TODO: SIMD
#if EULER
  p.Vx[i] += b[0][0] * p._1_mass[0] * p.Fx[i]; // b0 = dt
  p.Vy[i] += b[0][0] * p._1_mass[0] * p.Fy[i]; // b0 = dt
  p.Vz[i] += b[0][0] * p._1_mass[0] * p.Fz[i]; // b0 = dt
  p.Px[i] += b[0][0] * p.Vx[i];
  p.Py[i] += b[0][0] * p.Vy[i];
  p.Pz[i] += b[0][0] * p.Vz[i];
#else
  // Correction
  vec4f r = b[1] * (p.force[i] * p._1_mass - p.positionDerivatives[0][i]);
  p.position[i] += c[0]*r;
  p.velocity[i] += c[1]*r;
  p.positionDerivatives[0][i] += c[2]*r;
  p.positionDerivatives[1][i] += c[3]*r;
  p.positionDerivatives[2][i] += c[4]*r;

  // Prediction
  p.position[i] += b[0]*p.velocity[i];
  p.position[i] += b[1]*p.positionDerivatives[0][i];
  p.position[i] += b[2]*p.positionDerivatives[1][i];
  p.position[i] += b[3]*p.positionDerivatives[2][i];
  p.velocity[i] += b[0]*p.positionDerivatives[0][i];
  p.velocity[i] += b[1]*p.positionDerivatives[1][i];
  p.velocity[i] += b[2]*p.positionDerivatives[2][i];
  p.positionDerivatives[0][i] += b[0]*p.positionDerivatives[1][i];
  p.positionDerivatives[0][i] += b[1]*p.positionDerivatives[2][i];
  p.positionDerivatives[1][i] += b[0]*p.positionDerivatives[2][i];
#endif
 }

 struct Plate : Vertex {
  //struct { NoOperation4 operator[](size_t) const { return {}; }} torque;

  sconst float curvature = 0;
  sconst float elasticModulus = 1e9 * kg/(m*s*s); // 1 GPa

  Plate() : Vertex(0, 2, 1e-3 * densityScale) {
   count = 2;
   Px.clear(); Py.clear(); Pz.clear();
   Vx.clear(); Vy.clear(); Vz.clear();
   Fx.clear(); Fy.clear(); Fz.clear();
   //for(size_t i: range(3)) { PDx[i].clear(); PDy[i].clear(); PDz[i].clear(); }
  }
 } plate;
 /*/// Sphere - Plate
 template<Type tA, Type tB> inline void contact(const tA& A, v8si a, Plate& B, v8si b) {
  if(b == 0) { // Bottom
   vec4f normal {0, 0, 1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.Px[a], A.Py[a], B.Pz[b], 0},
      normal, (A.Pz[a]-tA::radius) - B.Pz[b],0,0 };
  } else { // Top
   vec4f normal {0, 0, -1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.Px[a], A.Py[a], B.Pz[b], 0},
      normal, B.Pz[b] - (A.Pz[a]+tA::radius),0,0 };
  }
 }*/

 struct Grain : Vertex {
  // Properties
  sconst float radius =  2.47*mm; // 40 mm diameter
  sconst v8sf radius8 = float8(radius); // 40 mm diameter
  sconst float volume = 4./3 * PI * cb(radius);
  sconst float curvature = 1./radius;
  sconst float shearModulus = 79e9 * kg / (m*s*s);
  sconst float poissonRatio = 0.28;
  sconst float elasticModulus = 0 ? 2*shearModulus*(1+poissonRatio) : 1e10; // ~2e11

  sconst float density = 7.8e3 * densityScale;
  sconst float mass = density * volume;
  sconst float angularMass = 2./5*mass*sq(radius);

  const float dt_angularMass;
  vec3 g;

  buffer<vec4f> rotation { capacity };
  buffer<float> AVx { capacity }, AVy { capacity }, AVz { capacity }; // Angular velocity
  buffer<float> Tx { capacity }, Ty { capacity }, Tz { capacity }; // torque

  Grain(const System& system) : Vertex(2, 3852*4/*=15408*/, Grain::mass), // 5 MB
   dt_angularMass(system.dt/angularMass),
    g(mass * system.G) {
   //for(size_t i: range(3)) angularDerivatives[i] = buffer<vec4f>(capacity);
  }
 } grain {*this};

 const vec4f dt_2 = float4(dt/2);
 void step(Grain& p, size_t i) {
  step((Vertex&)p, i);
   // Rotation viscosity FIXME
  p.AVx[i] *= 1-10*dt; p.AVy[i] *= 1-10*dt; p.AVz[i] *= 1-10*dt;
  //p.AVx[i] *= 1./2; p.AVy[i] *= 1./2; p.AVz[i] *= 1./2;
  // Euler
  p.rotation[i] += dt_2 * qmul((v4sf){p.AVx[i],p.AVy[i],p.AVz[i],0}, p.rotation[i]);
  p.AVx[i] += p.dt_angularMass * p.Tx[i];
  p.AVy[i] += p.dt_angularMass * p.Ty[i];
  p.AVz[i] += p.dt_angularMass * p.Tz[i];
  p.rotation[i] *= rsqrt(sq4(p.rotation[i]));
 }

 struct Wire : Vertex {
  using Vertex::Vertex;
  //struct { NoOperation4 operator[](size_t) const { return {}; }} torque;

  sconst float radius = 0.5 * mm; // 2mm diameter
  sconst float curvature = 1./radius;
  sconst float internodeLength = Grain::radius/2;
  sconst vec4f internodeLength4 = float3(internodeLength);
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1e3 * kg / cb(m) * densityScale;
  sconst float angularMass = 0;
  sconst float elasticModulus = 1e7; // ~ 1e7
  const vec4f tensionStiffness = float3(100*/*FIXME*/ elasticModulus * PI * sq(radius));
  //sconst vec4f tensionDamping = _0f; //float3(mass / s);
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = 0; //10*/*FIXME*/ elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float mass = Wire::density * Wire::volume;
  sconst float bendDamping = 0; //mass / s;
  //float tensionEnergy=0;
  Wire(float elasticModulus, size_t base) :
    Vertex{base, 1<<16, Wire::mass}/*, elasticModulus(elasticModulus)*/ {
   assert_(elasticModulus == Wire::elasticModulus);
  }
 } wire;

 struct Side : Vertex {
  sconst float curvature = 0; // -1/radius?
  sconst float elasticModulus = 1e8; // for contact
  sconst float density = 1e3;
  const float resolution;
  const float initialRadius;
  const float height;
  const size_t W, stride, H;
  const float radius = initialRadius;

  const float internodeLength = 2*PI*initialRadius/W;

  const float pourThickness;
  const float loadThickness;
  float thickness = pourThickness;
  //const float thickness;

  const float tensionElasticModulus; // for contact
  const float massCoefficient = sqrt(3.)/2 * sq(internodeLength) * densityScale * density;
  const float tensionCoefficient = sqrt(3.)/2 * internodeLength * tensionElasticModulus;
  float tensionStiffness = tensionCoefficient  * thickness; // FIXME

  float tensionDamping = mass / s;
  //sconst float areaMomentOfInertia = pow4(1*mm); // FIXME
  //const float bendStiffness = 0;//elasticModulus * areaMomentOfInertia / internodeLength; // FIXME

  //float tensionEnergy = 0, tensionEnergy2 = 0;

  //struct { v4sf operator[](size_t) const { return _0f; } } position;
  //struct { NoOperation4 operator[](size_t) const { return {}; }} torque;

  Side(float resolution, float initialRadius, float height, size_t base, float thickness, float elasticModulus)
   : Vertex(base, /*W*H*/7+(int(2*PI*initialRadius/resolution+7)/8*8+8) * (int(height/resolution*2/sqrt(3.))+1),
                1/*dummy*/),
        resolution(resolution),
        initialRadius(initialRadius),
        height(height),
        W(int(2*PI*initialRadius/resolution+7)/8*8),
        stride(W+8),
        H(int(height/resolution*2/sqrt(3.))+1),
        pourThickness(thickness*10),
        loadThickness(thickness),
        tensionElasticModulus(elasticModulus)
  {
      mass = massCoefficient*thickness;
      _1_mass = float3(1./mass);
      //count = W*H;
      for(size_t i: range(H)) {
       for(size_t j: range(W)) {
        float z = i*height/(H-1);
        float a = 2*PI*(j+(i%2)*1./2)/(W);
        float x = initialRadius*cos(a), y = initialRadius*sin(a);
        Px[7+i*stride+j] = x;
        Py[7+i*stride+j] = y;
        Pz[7+i*stride+j] = z;
       }
       // Copies position back to repeated nodes
       Px[7+i*stride+W+0] = Px[7+i*stride+0];
       Py[7+i*stride+W+0] = Py[7+i*stride+0];
       Pz[7+i*stride+W+0] = Pz[7+i*stride+0];
       Px[7+i*stride+W+1] = Px[7+i*stride+1];
       Py[7+i*stride+W+1] = Py[7+i*stride+1];
       Pz[7+i*stride+W+1] = Pz[7+i*stride+1];
      }
      Vx.clear(); Vy.clear(); Vz.clear();
#if GEAR
      for(size_t i: range(3)) positionDerivatives[i].clear(_0f);
#endif
  }
 } side;

 /*/// Side - Sphere
 template<Type tB>
 Contact contact(const Side& side, size_t vertexIndex, const tB& B, size_t bIndex) {
  vec4f a = {side.Px[vertexIndex], side.Py[vertexIndex], side.Pz[vertexIndex]};
  vec4f relativePosition = a - B.position[bIndex];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tB::radius)*(-normal), _0f, normal, length[0]-tB::radius, 0, 0};
 }
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t aIndex, const Side& side, size_t vertexIndex) {
  vec4f b = {side.Px[vertexIndex], side.Py[vertexIndex], side.Pz[vertexIndex]};
  vec4f relativePosition = A.position[aIndex] - b;
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tA::radius)*(-normal), _0f, normal, length[0]-tA::radius, 0, 0};
 }*/

 struct RigidSide {
  sconst size_t base = 0;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e8;
  float radius;
  RigidSide(float radius) : radius(radius) {}
  /*struct { v4sf operator[](size_t) const { return _0f; } } position;
  struct { v4sf operator[](size_t) const { return _0f; } } velocity;
  struct { NoOperation operator[](size_t) const { return {}; }} Fx, Fy, Fz;
  struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
  struct { v4sf operator[](size_t) const { return _0f; } } angularVelocity;*/
  //struct { NoOperation4 operator[](size_t) const { return {}; }} torque;
 } rigidSide {side.radius};

 /*/// Sphere - Rigid side
 template<Type tA>
 Contact contact(const tA& A, size_t aIndex, const RigidSide& side, size_t) {
  float length = length2(A.position[aIndex]);
  float r = side.radius;
  vec4f normal {-A.position[aIndex][0]/length, -A.position[aIndex][1]/length, 0, 0};
  return { float3(tA::radius)*(-normal),
     vec4f{r*-normal[0], r*-normal[1], A.position[aIndex][2], 0},
   normal, r-tA::radius-length,0,0 };
 }*/

 System(const Dict& p) :
   dt(p.at("TimeStep"_)),
   gz(10/*p.at("G")*/),
   //frictionCoefficient(p.at("Friction"_)),
   wire(p.value("Elasticity"_, /*0.f*/1e7), grain.base+grain.capacity),
   side(Grain::radius/(float)p.at/*value*/("Resolution"/*,2*/), p.at("Radius"_),
        /*p.at("Height"_)*/ (float)p.at("Radius"_)*4.f,
          /*p.at("Thickness"_),*/ wire.base+wire.capacity, p.at("Thickness"_), p.at("Side")) {
  //log("System");
     if(p.at("Pattern")!="none"_) assert_(wire.elasticModulus);
     assert_((float)p.at("Friction"_) == frictionCoefficient);
 }

 // Update
 size_t timeStep = 0;
 float grainKineticEnergy=0, wireKineticEnergy=0, normalEnergy=0, staticEnergy=0;
 //bendEnergy=0;

 /*bool recordContacts = false;
 struct ContactForce { size_t a, b; vec3 relativeA, relativeB; vec3 force; };
 array<ContactForce> contacts, contacts2;*/

 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 size_t staticFrictionCount2 = 0, dynamicFrictionCount2 = 0;

 /// Evaluates contact force between an object and an obstacle without friction
 template<Type tA, Type tB> inline void contact(const tA& A, v8si a,
                                         v8sf depth,
                                         v8sf RAx, v8sf RAy, v8sf RAz,
                                         v8sf Nx, v8sf Ny, v8sf Nz,
                                         v8sf& Fx, v8sf& Fy, v8sf& Fz
                                         ) {
  // Stiffness
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R);
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ;
  v8sf fN = fK + fB;
  Fx = fN * Nx;
  Fy = fN * Ny;
  Fz = fN * Nz;
 }

 /// Evaluates contact force between two objects without friction (non rotating B)
 template<Type tA, Type tB> inline void contact(const tA& A, v8si a, tB& B, v8si b,
                                        v8sf depth,
                                        v8sf RAx, v8sf RAy, v8sf RAz,
                                        v8sf Nx, v8sf Ny, v8sf Nz,
                                        v8sf& Fx, v8sf& Fy, v8sf& Fz) {
  // Stiffness
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  static const float K = 4./3*E*sqrt(R); // FIXME: constexpr sqrt
  static const v8sf K8 = float8(K);

  const v8sf Ks = K8 * sqrt8(depth);
  const v8sf fK = Ks * depth;

  // Damping
  static const v8sf KB = float8(K * normalDamping);
  const v8sf Kb = KB * sqrt8(depth);
  v8sf AVx = gather(A.AVx, a), AVy = gather(A.AVy, a), AVz = gather(A.AVz, a);
  v8sf RVx = gather(A.Vx, a) + (AVy*RAz - AVz*RAy) - gather(B.Vx, b);
  v8sf RVy = gather(A.Vy, a) + (AVz*RAx - AVx*RAz) - gather(B.Vy, b);
  v8sf RVz = gather(A.Vz, a) + (AVx*RAy - AVy*RAx) - gather(B.Vz, b);
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * normalSpeed ; // Damping
  v8sf fN = fK + fB;
  Fx = fN * Nx;
  Fy = fN * Ny;
  Fz = fN * Nz;
 }

 /// Evaluates contact force between two objects with friction (rotating B)
 template<Type tA, Type tB> inline void contact(
   const tA& A, v8si a,
   tB& B, v8si b,
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
  for(size_t k: range(8)) { // FIXME
   if(!localAx[k]) {
    v4sf localA = qapply(conjugate(A.rotation[a[k]]), (v4sf){RAx[k], RAy[k], RAz[k], 0});
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
    v4sf localB = qapply(conjugate(B.rotation[b[k]]), (v4sf){RBx[k], RBy[k], RBz[k], 0});
    localBx[k] = localB[0];
    localBy[k] = localB[1];
    localBz[k] = localB[2];
    //log(localAx[k], localAy[k], localAz[k]);
    //log(localBx[k], localBy[k], localBz[k]);
   }
   vec4f relativeA = qapply(A.rotation[a[k]], (v4sf){localAx[k], localAy[k], localAz[k], 0});
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   vec4f relativeB = qapply(B.rotation[b[k]], (v4sf){localBx[k], localBy[k], localBz[k], 0});
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
  sconst v8sf staticFrictionStiffness = float8(staticFrictionFactor * frictionCoefficient);
  v8sf kS = staticFrictionStiffness * fK; //fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN
  //for(size_t k: range(8)) log(fS[k]);

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = frictionCoefficient8 * fK; //fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(8)) { // FIXME: mask
   if(fS[k] < fD[k] && tangentLength[k] < staticFrictionLength) {
    // Static
    if(tangentLength[k]) {
     vec4f springDirection = vec4f{TOx[k], TOy[k], TOz[k], 0} / float4(tangentLength[k]);
     float fB = staticFrictionDamping * dot3(springDirection, v4sf{RVx[k], RVy[k], RVz[k], 0})[0];
     fTx[k] = - (fS[k]+fB) * springDirection[0];
     fTy[k] = - (fS[k]+fB) * springDirection[1];
     fTz[k] = - (fS[k]+fB) * springDirection[2];
     //log("static", fTx[k], fTy[k], fTz[k]);
     staticFrictionCount++;
    } else {
     fTx[k] = 0;
     fTy[k] = 0;
     fTz[k] = 0;
    }
   } else {
    // Dynamic
    localAx[k] = 0; // if(!localAx[k]) {
    if(tangentRelativeSpeed[k]) {
     float scale = - fD[k] / tangentRelativeSpeed[k];
     fTx[k] = scale * TRVx[k];
     fTy[k] = scale * TRVy[k];
     fTz[k] = scale * TRVz[k];
     //log("dynamic", k, fTx[k], fTy[k], fTz[k], fD[k], tangentRelativeSpeed[k], scale, TRVx[k],TRVy[k],TRVz[k]);
     dynamicFrictionCount++;
    } else {
     fTx[k] = 0;
     fTy[k] = 0;
     fTz[k] = 0;
    }
   }
  }
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

constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr vec4f System::Wire::internodeLength4;
constexpr float System::Wire::elasticModulus;
constexpr float System::Plate::elasticModulus;
constexpr float System::Side::density;
constexpr float System::Grain::elasticModulus;
//constexpr vec4f System::Wire::tensionDamping;
constexpr float System::RigidSide::elasticModulus;
