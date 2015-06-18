#include "simd.h"
float length2(v4sf v) { return sqrt(sq2(v))[0]; }
float length(v4sf v) { return sqrt(sq3(v))[0]; }

struct RadialSum {
 real radialSum;
 RadialSum& operator[](size_t){ return *this; }
 void operator -=(v4sf f) { radialSum += length2(f); }
};
void atomic_sub(RadialSum& t, v4sf f) { t.radialSum += length2(f); }

struct System {
#define sconst static constexpr
 sconst bool rollTest = 0;
 sconst bool useWire = 1 && !rollTest;

 // Characteristic dimensions (SI)
 sconst real T = 1 /* 1 s */, L = 1 /* 1 m */, M = 1 /* 1 kg */;
 sconst real loopAngle = 0 ? (0 ? PI*(3-sqrt(5.)) : (2*PI)/5) : 0;
 sconst real staticFrictionSpeed = 1 *L/T;
 sconst real staticFrictionFactor = 1; //0x1p5 /L;
 sconst real staticFrictionDamping = 0x1p-6 *M/T/T;
 sconst real dynamicFriction = 1./8/*16*/ *M/T;
 sconst real dampingFactor = 0x1p-11; //0x1p-8;
 sconst real wireBendStiffness = 0x1p-13; //12-16
 sconst real wireBendDamping = 0x1p-13; //12-16
 sconst real winchRate = 128 /T;
 sconst real winchSpeed = 1./6 *L/T;
 sconst v4sf g {0, 0, -10, 0}; // N/kg = m·s¯²
 sconst real dt = 1./(60*512); // 3·10¯⁵ s (~1/14 RT)
 #define DBG_FRICTION 0

 struct Contact {
  v4sf relativeA, relativeB; // Relative to center but world axices
  v4sf normal;
  real depth;
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB>
 Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
  v4sf relativePosition = A.position[a] - B.position[b];
  v4sf length = sqrt(sq3(relativePosition));
  v4sf normal = relativePosition/length; // B -> A
  return {float4(tA::radius)*(-normal), float4(tB::radius)*(+normal),
     normal, length[0]-tA::radius-tB::radius};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  v4sf localA, localB; // Last static friction contact location
  size_t lastUpdate = 0;
  bool operator==(const Friction& b) const {
   return index == b.index;
  }
 };

 struct Obstacle {
  sconst real mass = inf;
  sconst real angularMass = 0;
  sconst size_t count = 1;
  struct { v4sf operator[](size_t){ return _0f; }} position;
  struct { v4sf operator[](size_t){ return _0f; }} velocity;
  struct { v4sf operator[](size_t){ return _0f; }} rotation;
  struct { v4sf operator[](size_t){ return _0f; }} angularVelocity;
  v4sf force[1];
 };

 struct Floor : Obstacle {
  sconst size_t base = 0;
  sconst real radius = inf;
  sconst real height = 0; //8*L/256; //Wire::radius;
  sconst real curvature = 0;
  sconst real elasticModulus = 1e5 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;
 } floor;
 /// Sphere - Floor
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Floor&, size_t) {
  v4sf normal {0, 0, 1, 0};
  return { float4(tA::radius)*(-normal),
               v4sf{A.position[a][0], A.position[a][1], Floor::height, 0},
               normal, A.position[a][2]-tA::radius-Floor::height };
 }

 struct Side : Obstacle {
  sconst size_t base = 1;
  sconst real curvature = 0; // -1/radius?
  sconst real elasticModulus = 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M/T;
  sconst real frictionCoefficient = 1;
  real castRadius = Grain::radius*10;
  RadialSum force;
 } side;
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Side& side, size_t) {
  float length = length2(A.position[a]);
  v4sf normal {-A.position[a][0]/length, -A.position[a][1]/length, 0, 0}; // Side -> Sphere
  return { float4(tA::radius)*(-normal),
     v4sf{side.castRadius*-normal[0], side.castRadius*-normal[1], A.position[a][2], 0},
               normal, side.castRadius-tA::radius-length };
 }

 struct Particle {
  const size_t base, capacity;
  const v4sf mass;
  buffer<v4sf> position { capacity };
  buffer<v4sf> velocity { capacity };
  buffer<v4sf> positionDerivatives[3]; // { [0 ... 2] = buffer<v4sf>(capacity) };
  buffer<v4sf> force { capacity };
  buffer<array<Friction>> frictions { capacity };
  size_t count = 0;

  struct { v4sf operator[](size_t) const { return _0f; }} rotation;
  struct { v4sf operator[](size_t) const { return _0f; }} angularVelocity;

  Particle(size_t base, size_t capacity, real mass) : base(base), capacity(capacity),
    mass(float4(mass)) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<v4sf>(capacity);
   frictions.clear();
  }
  sconst v4sf b[] = {float4(dt), float4(dt*dt/2), float4(dt*dt*dt/6), float4(dt*dt*dt*dt/24)};
  sconst v4sf c[] = {float4(19./90), float4(3/(4*dt)), float4(2/(dt*dt)), float4(6/(2*dt*dt*dt)),
                                 float4(24/(12*dt*dt*dt*dt))};
  void step(size_t i) {
   // Correction
   v4sf r = b[1] * (force[i] / mass - positionDerivatives[0][i]);
   position[i] += c[0]*r;
   velocity[i] += c[1]*r;
   positionDerivatives[0][i] += c[2]*r;
   positionDerivatives[1][i] += c[3]*r;
   positionDerivatives[2][i] += c[4]*r;

   // Prediction
   position[i] += b[0]*velocity[i];
   position[i] += b[1]*positionDerivatives[0][i];
   position[i] += b[2]*positionDerivatives[1][i];
   position[i] += b[3]*positionDerivatives[2][i];
   velocity[i] += b[0]*positionDerivatives[0][i];
   velocity[i] += b[1]*positionDerivatives[1][i];
   velocity[i] += b[2]*positionDerivatives[2][i];
   positionDerivatives[0][i] += b[0]*positionDerivatives[1][i];
   positionDerivatives[0][i] += b[1]*positionDerivatives[2][i];
   positionDerivatives[1][i] += b[0]*positionDerivatives[2][i];
  }
 };

 struct Grain : Particle {
  sconst size_t base = 2;

  // Properties
  sconst real radius = 2e-2; // 40 mm diameter
  sconst real curvature = 1./radius;
  sconst real elasticModulus = 1e5 * M / (L*T*T); // 1e6
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;

  const float angularMass = 2./5*mass[0]*(pow5(radius)-pow5(radius-1e-4))
                                         / (cb(radius)-cb(radius-1e-4));
  const v4sf dt_angularMass = float4(dt/angularMass);
  const v4sf _1_angularMass = float4(1/angularMass);
  buffer<v4sf> rotation { capacity };
  buffer<v4sf> angularVelocity { capacity };
  buffer<v4sf> angularDerivatives[3]; // { [0 ... 2] = buffer<v4sf>(capacity) };
  Grain(size_t base, size_t capacity) : Particle(base, capacity, 3e-3) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<v4sf>(capacity);
  }
  buffer<v4sf> torque { capacity };

  sconst v4sf dt_2 = float4(dt/2);
  sconst v4sf viscosity = float4(1-30*dt); // 1-x/60/512
  void step(size_t i) {
   Particle::step(i);
   torque[i][3] = 0;
   //assert_(torque[i][3] == 0, torque[i]);
   angularVelocity[i][3] = 0;
   //assert_(angularVelocity[i][3] == 0, angularVelocity[i][3]);
#if 1
   rotation[i] += dt_2 * qmul(rotation[i], angularVelocity[i]);
   angularVelocity[i] += dt_angularMass * torque[i]; //-cross(w, Grain::angularMass*w)
   angularVelocity[i] *= viscosity;
#elif 0 // -15%
   // Correction
   v4sf r = b[1] * (torque[i]  * _1_angularMass - angularDerivatives[0][i]);
   rotation[i] += qmul(rotation[i], c[0]*r);
   angularVelocity[i] += c[1]*r;
   for(size_t n: range(3)) angularDerivatives[n][i] += c[2+n]*r;

   /*// Prediction
   rotation[i] += qmul(rotation[i], float4(dt)*angularVelocity[i]);
   for(size_t n: range(3)) {
    rotation[i] += qmul(rotation[i], b[1+n]*angularDerivatives[n][i]);
    angularVelocity[i] += b[n]*angularDerivatives[n][i];
    for(size_t j: range(3-(n+1)))
     angularDerivatives[n][i] += b[j]*angularDerivatives[n+1+j][i];
   }*/
   // Prediction
   rotation[i] += b[0]*qmul(rotation[i], angularVelocity[i]);
   rotation[i] += b[1]*qmul(rotation[i], angularDerivatives[0][i]);
   rotation[i] += b[2]*qmul(rotation[i], angularDerivatives[1][i]);
   rotation[i] += b[3]*qmul(rotation[i], angularDerivatives[2][i]);
   angularVelocity[i] += b[0]*angularDerivatives[0][i];
   angularVelocity[i] += b[1]*angularDerivatives[1][i];
   angularVelocity[i] += b[2]*angularDerivatives[2][i];
   angularDerivatives[0][i] += b[0]*angularDerivatives[1][i];
   angularDerivatives[0][i] += b[1]*angularDerivatives[2][i];
   angularDerivatives[1][i] += b[0]*angularDerivatives[2][i];
#endif
   rotation[i] *= rsqrt(sq4(rotation[i]));
  }
 } grain {0, rollTest ? 2 : 1024};

 struct Wire : Particle {
  using Particle::Particle;
  sconst real radius = 1e-3; // 2mm diameter
  sconst real curvature = 1./radius;
  sconst real internodeLength = 1./4 * Grain::radius;
  sconst v4sf internodeLength4 = float4(internodeLength);
  sconst real volume = PI * sq(radius) * internodeLength;
  sconst real density = 1e3 * M / cb(L);
  sconst real angularMass = 0;
  sconst real elasticModulus = 1e8 * M / (L*T*T);
  sconst real normalDamping = dampingFactor * M / T; // 0.3
  sconst real frictionCoefficient = 1;

  sconst v4sf tensionStiffness = float4(1 * elasticModulus*PI*sq(radius));
  const v4sf tensionDamping = float4(0/*x1p-2*/ * mass[0]/T);
 } wire {grain.base+grain.capacity, useWire ? 5*1024 : 0,
         Wire::density * Wire::volume};

 struct Load : Particle {
  using Particle::Particle;
  sconst real curvature = 0;
  sconst real elasticModulus = 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;
 } load {wire.base+wire.capacity, 1, 1./16 * M};
 /// Sphere - Load
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Load& B, size_t) {
  vec3f normal (0, 0, -1);
  return { float(tA::radius)*(-normal),
     vec3f(A.position[a][0], A.position[a][1], B.position[0][2]),
     normal, B.position[0][2]-(A.position[a][2]+tA::radius) };
 }

 // Update
 size_t timeStep = 0;
 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 real potentialEnergy = 0;

 /// Evaluates contact penalty between two objects
 /// \return torque on A
 template<Type tA, Type tB>
 v4sf penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return _0f;
  // Stiffness
  constexpr real E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr real R = 1/(tA::curvature+tB::curvature);
  const real K = 2./3*E*sqrt(R);
  const real Ks = K * sqrt(-c.depth);
  potentialEnergy += 1./2 * Ks * sq(c.depth); // FIXME: damping ~ f(x)
  real fK = - Ks * c.depth;
  // Damping
  const real Kb = K*(tA::normalDamping+tB::normalDamping)/2*sqrt(-c.depth);
  v4sf relativeVelocity =
      A.velocity[a]// + cross(A.angularVelocity[a], c.relativeA)
   - (B.velocity[b]);// + cross(B.angularVelocity[b], c.relativeB));
  real normalSpeed = dot3(c.normal, relativeVelocity)[0];
  real fB = - Kb * normalSpeed ; // Damping
  real fN = fK + fB;
  v4sf force = float4(fN) * c.normal;

  Friction& friction = A.frictions[a].add(Friction{B.base+b, _0f, _0f});
  if(friction.lastUpdate < timeStep-1) { // Resets contact (TODO: GC)
   friction.localA = qapply(conjugate(A.rotation[a]), c.relativeA);
   friction.localB = qapply(conjugate(B.rotation[b]), c.relativeB);
  }
  friction.lastUpdate = timeStep;

  constexpr real frictionCoefficient
    = min(tA::frictionCoefficient, tB::frictionCoefficient);
  v4sf relativeA = qapply(A.rotation[a], friction.localA);
  v4sf relativeB = qapply(B.rotation[b], friction.localB);
  v4sf globalA = A.position[a] + relativeA;
  v4sf globalB = B.position[b] +  relativeB;
  v4sf x = globalA - globalB;
  v4sf tangentOffset = x - dot3(c.normal, x) * c.normal;
  v4sf tangentLength = sqrt(sq3(tangentOffset));
  real staticFrictionStiffness = staticFrictionFactor * frictionCoefficient;
  real kS = staticFrictionStiffness * fN;
  real fS = kS * tangentLength[0]; // 0.1~1 fN

  v4sf tangentRelativeVelocity
    = relativeVelocity - dot3(c.normal, relativeVelocity) * c.normal;
  v4sf tangentRelativeSpeed = sqrt(sq3(tangentRelativeVelocity));
  real fD = frictionCoefficient * fN; // * tangentRelativeSpeed ?
  v4sf fT;
  assert_(isNumber(fS), x, c.normal, globalA, globalB, relativeA, relativeB, A.rotation[a], B.rotation[b], friction.localA, friction.localB);
  if(fS < fD) {
   if(tangentLength[0]) {
    v4sf springDirection = tangentOffset / tangentLength;
    real fB = staticFrictionDamping * dot3(springDirection, relativeVelocity)[0];
    fT = - float4(fS+fB) * springDirection;
    staticFrictionCount++;
   } else fT = _0f;
  } else {
   if(tangentRelativeSpeed[0]) {
    fT = - float4(fD) * tangentRelativeVelocity / tangentRelativeSpeed;
    dynamicFrictionCount++;
   } else fT = _0f;
  }
  force += fT;
  A.force[a] += force;
  //B.force[b] -= force;
  atomic_sub(B.force[b], force);
  return cross(relativeA, fT);
 }
};
constexpr real System::dt;
constexpr v4sf System::Particle::b[];
constexpr v4sf System::Particle::c[];
constexpr v4sf System::g;
constexpr real System::Grain::radius;
