#include "simd.h"
typedef v4sf vec4f;
float length2(vec4f v) { return sqrt(sq2(v))[0]; }
float length(vec4f v) { return sqrt(sq3(v))[0]; }

#define sconst static constexpr

struct NoOperation {
  float operator [](size_t) const { return 0; }
};
string str(NoOperation) { return "NoOperation"; }
void operator +=(NoOperation, vec4f) {}
void operator -=(NoOperation, vec4f) {}

struct RadialSum {
 float radialSum;
 RadialSum& operator[](size_t){ return *this; }
 void operator -=(vec4f f) { radialSum += length2(f); }
};
void atomic_sub(RadialSum& t, vec4f f) { t.radialSum += length2(f); }

struct System {
 sconst float dt = 1./(60*512);

 // Characteristic dimensions (SI)
 sconst float T = 1 /* 1 s */, L = 1 /* 1 m */, M = 1 /* 1 kg */;
 sconst vec4f g {0, 0, -10, 0}; // N/kg = m·s¯²

 // Penalty model
 sconst float dampingFactor = 0x1p-11; // 9-13
 // Friction model
 sconst float staticFrictionSpeed = 1./3 *L/T;
 sconst float staticFrictionFactor = 0x1p8 /L;
 sconst float staticFrictionLength = 8e-3;
 sconst float staticFrictionDamping = 0x1p-6 *M/T/T; // 3-6-9-13
 sconst float wireBendStiffness = 0x1p-12; //11-14 [F/a=MLT-2]
 sconst float wireBendDamping = 0x1p-13; //12-14
 // Process
 const float loopAngle = 1 ? (1 ? 2*PI*(3-sqrt(5.)) : PI/2) : 0;
 sconst float winchRate = 512 /T;
 sconst float winchSpeed = 1./2 *L/T;

 struct Contact {
  vec4f relativeA, relativeB; // Relative to center but world axices
  vec4f normal;
  float depth;
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB>
 Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
  vec4f relativePosition = A.position[a] - B.position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tA::radius)*(-normal), float3(tB::radius)*(+normal),
     normal, length[0]-tA::radius-tB::radius};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  vec4f localA, localB; // Last static friction contact location
  size_t lastStatic = 0;
#if DBG_FRICTION
  size_t lastFriction = 0;
  vec4f /*red = _0f, green = _0f,*/ blue = _0f;
  rgb3f color = 0;
#endif
  bool operator==(const Friction& b) const {
   return index == b.index;
  }
 };

 struct Obstacle {
  sconst float mass = inf;
  sconst float angularMass = 0;
  sconst size_t count = 1;
  struct { vec4f operator[](size_t){ return _0f; }} position;
  struct { vec4f operator[](size_t){ return _0f; }} velocity;
  struct { vec4f operator[](size_t){ return _0001f; }} rotation;
  struct { vec4f operator[](size_t){ return _0f; }} angularVelocity;
  vec4f force[1];
  vec4f torque[1];
 };

 struct Floor : Obstacle {
  sconst size_t base = 0;
  sconst float radius = inf;
  sconst float height = 0; //8*L/256; //Wire::radius;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e5 * M/(L*T*T);
  sconst float normalDamping = dampingFactor * M / T;
  sconst float frictionCoefficient = 1;
  sconst float staticFrictionThresholdSpeed = staticFrictionSpeed;
 } floor;
 /// Sphere - Floor
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Floor&, size_t) {
  vec4f normal {0, 0, 1, 0};
  return { float3(tA::radius)*(-normal),
               vec4f{A.position[a][0], A.position[a][1], Floor::height, 0},
               normal, (A.position[a][2]-tA::radius)-Floor::height };
 }

 struct Side : Obstacle {
  sconst size_t base = 1;
  sconst float curvature = 0; // -1/radius?
  sconst float elasticModulus = 1e5 * M/(L*T*T); // 5
  sconst float normalDamping = dampingFactor * M/T;
  sconst float frictionCoefficient = 1;
  const float initialRadius = Grain::radius*8;
  float castRadius = initialRadius;
  RadialSum force;
 } side;
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Side& side, size_t) {
  float length = length2(A.position[a]);
  // Side -> Sphere
  float r = side.castRadius;
  vec4f normal {-A.position[a][0]/length, -A.position[a][1]/length, 0, 0};
  return { float3(tA::radius)*(-normal),
     vec4f{r*-normal[0], r*-normal[1], A.position[a][2], 0},
               normal, r-tA::radius-length };
 }

 /*struct Lid : Obstacle {
  sconst size_t base = 3;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e5 * M/(L*T*T);
  sconst float normalDamping = dampingFactor * M / T;
  sconst float frictionCoefficient = 1;
 } lid;
 /// Sphere - Lid side
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Lid&, size_t) {
  float length = length2(A.position[a]);
  //if(length >= side.initialRadius) return {};
  vec4f b {A.position[a][0]/length*side.initialRadius,
     A.position[a][1]/length*side.initialRadius,
     (load.position[0][2]-Grain::radius), 0};
  //float z = A.position[a][2] - (load.position[0][2]-Grain::radius);
  //float x = side.initialRadius - length;
  //float d = sqrt(x*x+z*z);
  vec4f r = A.position[a] - b;
  float length3 = ::length(r);
  vec4f normal = r/float4(length3);
  return {float3(tA::radius)*(-normal), b, normal, length3-tA::radius};
 }*/

 struct Particle {
  const size_t base, capacity;
  const float mass;
  const vec4f _1_mass;
  buffer<vec4f> position { capacity };
  buffer<vec4f> velocity { capacity };
  buffer<vec4f> positionDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };
  buffer<vec4f> force { capacity };
  buffer<array<Friction>> frictions { capacity };
  //buffer<array<String>> history { capacity }; // Debug
  size_t count = 0;

  struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
  struct { vec4f operator[](size_t) const { return _0f; }} angularVelocity;

  Particle(size_t base, size_t capacity, float mass) : base(base), capacity(capacity),
    mass(mass), _1_mass(float3(1./mass)) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<vec4f>(capacity);
   frictions.clear();
   //history.clear();
  }
  sconst vec4f b[] = {float3(dt), float3(dt*dt/2), float3(dt*dt*dt/6), float3(dt*dt*dt*dt/24)};
  sconst vec4f c[] = {float3(19./90), float3(3/(4*dt)), float3(2/(dt*dt)), float3(6/(2*dt*dt*dt)),
                                 float3(24/(12*dt*dt*dt*dt))};

  void step(size_t i) {
   // Correction
   vec4f r = b[1] * (force[i] * _1_mass - positionDerivatives[0][i]);
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
  sconst size_t base = 2; //lid.base+1;

  // Properties
  sconst float radius = 2e-2; // 40 mm diameter
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1e6 * M / (L*T*T); // 1e6
  sconst float normalDamping = dampingFactor * M / T;
  sconst float frictionCoefficient = 1;

  const float angularMass = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                                         / (cb(radius)-cb(radius-1e-4));
  const vec4f dt_angularMass = float3(dt/angularMass);
  const vec4f _1_angularMass = float3(1/angularMass);
  buffer<vec4f> rotation { capacity };
  buffer<vec4f> angularVelocity { capacity };
  buffer<vec4f> angularDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };
  Grain(size_t base, size_t capacity) : Particle(base, capacity, 3e-3) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<vec4f>(capacity);
  }
  buffer<vec4f> torque { capacity };

  sconst vec4f dt_2 = float4(dt/2);
  void step(size_t i) {
   Particle::step(i);
#if 1
   rotation[i] += dt_2 * qmul(angularVelocity[i], rotation[i]);
   angularVelocity[i] += dt_angularMass * torque[i]; //-cross(w, Grain::angularMass*w)
#elif 1 // -15%
   // Correction
   vec4f r = b[1] * (torque[i]  * _1_angularMass - angularDerivatives[0][i]);
   rotation[i] += qmul(c[0]*r, rotation[i]);
   angularVelocity[i] += c[1]*r;
   for(size_t n: range(3)) angularDerivatives[n][i] += c[2+n]*r;

   // Prediction
   rotation[i] += b[0]*qmul(angularVelocity[i], rotation[i]);
   rotation[i] += b[1]*qmul(angularDerivatives[0][i], rotation[i]);
   rotation[i] += b[2]*qmul(angularDerivatives[1][i], rotation[i]);
   rotation[i] += b[3]*qmul(angularDerivatives[2][i], rotation[i]);
   angularVelocity[i] += b[0]*angularDerivatives[0][i];
   angularVelocity[i] += b[1]*angularDerivatives[1][i];
   angularVelocity[i] += b[2]*angularDerivatives[2][i];
   angularDerivatives[0][i] += b[0]*angularDerivatives[1][i];
   angularDerivatives[0][i] += b[1]*angularDerivatives[2][i];
   angularDerivatives[1][i] += b[0]*angularDerivatives[2][i];
#endif
   rotation[i] *= rsqrt(sq4(rotation[i]));
  }
 } grain {0, 512};

 struct Wire : Particle {
  using Particle::Particle;
  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  sconst float radius = 1e-3; // 2mm diameter
  sconst float curvature = 1./radius;
  sconst float internodeLength = Grain::radius/2;
  sconst vec4f internodeLength4 = float3(internodeLength);
  sconst float volume = PI * sq(radius) * internodeLength;
  sconst float density = 1e3 * M / cb(L);
  sconst float angularMass = 0;
  sconst float elasticModulus = 1e6 * M / (L*T*T); // 6-8
  sconst float normalDamping = dampingFactor * M / T; // 0.3
  sconst float frictionCoefficient = 1;

  sconst vec4f tensionStiffness = float3(10/*-1000*/ * elasticModulus*PI*sq(radius));
  const vec4f tensionDamping = float3(1/*./2*/ * mass/T);
 } wire {grain.base+grain.capacity, 4096, Wire::density * Wire::volume};
  /// Cylinder - Cylinder
  Contact contact(const Wire&, size_t a, const Wire&, size_t b) {
   if(a+1>=wire.count || b+1>wire.count) return {_0f,_0f,_0f,0}; // FIXME
   vec4f A1 = wire.position[a], A2 = wire.position[a+1];
   vec4f B1 = wire.position[b], B2 = wire.position[b+1];
   vec4f gA, gB; closest(A1, A2, B1, B2, gA, gB);
   vec4f r = gA-gB;
   vec4f length = sqrt(sq3(r));
   return {gA-wire.position[a], gB-wire.position[b], r/length,
                length[0] - 2*Wire::radius};
  }

 struct Load : Particle {
  using Particle::Particle;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e5 * M/(L*T*T);
  sconst float normalDamping = dampingFactor * M / T;
  sconst float frictionCoefficient = 1;
  vec4f torque[1];
 } load {wire.base+wire.capacity, 1, 1./3/*4*/ * M};
 /// Sphere - Load
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Load& B, size_t) {
  vec4f normal {0, 0, -1, 0};
  return { float3(tA::radius)*(-normal),
     vec4f{A.position[a][0], A.position[a][1], B.position[0][2], 0},
     normal, B.position[0][2]-(A.position[a][2]+tA::radius) };
 }

 // Update
 size_t timeStep = 0;
 float releaseTime = 0;
 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 size_t staticFrictionCount2 = 0, dynamicFrictionCount2 = 0;
 float gravityEnergy = 0, contactEnergy = 0, tensionEnergy = 0, torsionEnergy = 0;

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB>
 void penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return;
  // Stiffness
  constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  const float K = 2./3*E*sqrt(R);
  const float Ks = K * sqrt(-c.depth);
  contactEnergy += 1./2 * Ks * sq(c.depth); // FIXME: damping ~ f(x)
  float fK = - Ks * c.depth;
  // Damping
  const float Kb = K*(tA::normalDamping+tB::normalDamping)/2*sqrt(-c.depth);
  vec4f relativeVelocity =
      A.velocity[a] + cross(A.angularVelocity[a], c.relativeA)
   - (B.velocity[b] + cross(B.angularVelocity[b], c.relativeB));
  float normalSpeed = dot3(c.normal, relativeVelocity)[0];
  float fB = - Kb * normalSpeed ; // Damping
  float fN = fK + fB;
  vec4f force = float3(fN) * c.normal;

  fN = fK + fB;

  Friction& friction = A.frictions[a].add(Friction{B.base+b, _0f, _0f});
  if(friction.lastStatic < timeStep-1) { // Resets contact (TODO: GC)
   friction.localA = qapply(conjugate(A.rotation[a]), c.relativeA);
   friction.localB = qapply(conjugate(B.rotation[b]), c.relativeB);
  }

  constexpr float frictionCoefficient
    = min(tA::frictionCoefficient, tB::frictionCoefficient);
  vec4f relativeA = qapply(A.rotation[a], friction.localA);
  vec4f relativeB = qapply(B.rotation[b], friction.localB);
  vec4f globalA = A.position[a] + relativeA;
  vec4f globalB = B.position[b] +  relativeB;
  vec4f x = globalA - globalB;
  vec4f tangentOffset = x - dot3(c.normal, x) * c.normal;
  vec4f tangentLength = sqrt(sq3(tangentOffset));
  float staticFrictionStiffness = staticFrictionFactor * frictionCoefficient;
  float kS = staticFrictionStiffness * fN;
  float fS = kS * tangentLength[0]; // 0.1~1 fN

  vec4f tangentRelativeVelocity
    = relativeVelocity - dot3(c.normal, relativeVelocity) * c.normal;
  vec4f tangentRelativeSpeed = sqrt(sq3(tangentRelativeVelocity));
  float fD = frictionCoefficient * fN;
  vec4f fT;
  if(fS < fD && tangentLength[0] < staticFrictionLength
    && tangentRelativeSpeed[0] < staticFrictionSpeed
     //&& tangentLength[0] // ?
     ) {
   friction.lastStatic = timeStep; // Otherwise resets contact next step
   if(tangentLength[0]) {
    vec4f springDirection = tangentOffset / tangentLength;
    float fB = staticFrictionDamping * dot3(springDirection, relativeVelocity)[0];
    fT = - float3(fS+fB) * springDirection;
    staticFrictionCount++;
#if DBG_FRICTION
    friction.color = rgb3f(0,1,0);
    friction.lastFriction = timeStep;
#endif
   } else fT = _0f;
  } else {
   if(tangentRelativeSpeed[0]) {
    // Caps fT for correct integration on impact (i.e only damp, not induce rotations)
    //fD = min(fD, 0/*1e-10f/dt*/ * tangentRelativeSpeed[0]);
    fT = - float3(fD) * tangentRelativeVelocity / (tangentRelativeSpeed/*+float3(1e-8f*staticFrictionSpeed)*/);
    dynamicFrictionCount++;
#if DBG_FRICTION
    friction.color = rgb3f(1,0,0);
    friction.lastFriction = timeStep;
#endif
   } else fT = _0f;
  }
#if DBG_FRICTION
  friction.blue = fT;
#endif
  force += fT;
  A.force[a] += force;
  //B.force[b] -= force;
  atomic_sub(B.force[b], force);
  A.torque[a] += cross(relativeA, fT);
  //atomic_sub(B.torque[b], cross(relativeB, fT));
  B.torque[b] -= cross(relativeB, fT);
 }
};
constexpr float System::dt;
constexpr vec4f System::Particle::b[];
constexpr vec4f System::Particle::c[];
constexpr vec4f System::g;
constexpr float System::Grain::radius;
