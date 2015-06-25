#include "simd.h"
#include "parallel.h"
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
void atomic_add(NoOperation, vec4f) {}
void atomic_sub(NoOperation, vec4f) {}

struct System {
 // Integration
 const int subStepCount;
 const float dt = 1./(60*subStepCount);
 const vec4f b[4] = {float3(dt), float3(dt*dt/2), float3(dt*dt*dt/6), float3(dt*dt*dt*dt/24)};
 const vec4f c[5] = {float3(19./90), float3(3/(4*dt)), float3(2/(dt*dt)), float3(6/(2*dt*dt*dt)),
                                float3(24/(12*dt*dt*dt*dt))};

 // SI units
 sconst float s = 1, m = 1, kg = 1, N = kg*m/s/s;
 sconst float mm = 1e-3*m, g = 1e-3*kg;
 sconst vec4f G {0, 0, -10 * N/kg/*=m·s¯²*/, 0};

 // Penalty model
 sconst float normalDamping = 1./4 * g/s; // 11-13
 // Friction model
 sconst float staticFrictionSpeed = 1./3 *m/s;
 sconst float staticFrictionFactor = 1/(2e-3 *m); // Wire diameter
 sconst float staticFrictionLength = 10 *mm;
 sconst float staticFrictionDamping = 15 *g/s/s; // 3-6-9-13
 const float frictionCoefficient;

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
  bool operator==(const Friction& b) const { return index == b.index; }
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
  sconst float height = 0; //8*m/256; //Wire::radius;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e5 * kg/(m*s*s);
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
  sconst float elasticModulus = 1e6 * kg/(m*s*s); // 5
  const float initialRadius = Grain::radius*8;
  float castRadius = initialRadius;
  struct { NoOperation operator[](size_t) const { return {}; }} force;
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

 struct Particle {
  const size_t base, capacity;
  float mass;
  const vec4f _1_mass;
  buffer<vec4f> position { capacity };
  buffer<vec4f> velocity { capacity };
  buffer<vec4f> positionDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };
  buffer<vec4f> force { capacity };
  buffer<array<Friction>> frictions { capacity };
  size_t count = 0;

  struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
  struct { vec4f operator[](size_t) const { return _0f; }} angularVelocity;

  Particle(size_t base, size_t capacity, float mass) : base(base), capacity(capacity),
    mass(mass), _1_mass(float3(1./mass)) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<vec4f>(capacity);
   frictions.clear();
  }
 };

 void step(Particle& p, size_t i) {
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
 }

 struct Grain : Particle {
  // Properties
  sconst float radius = 20*mm; // 40 mm diameter
  sconst float volume = 2./3 * PI * cb(radius);
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1e6 * kg / (m*s*s); // 1e6

  sconst float mass = 3*g;
  sconst float angularMass = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                                         / (cb(radius)-cb(radius-1e-4));
  const vec4f dt_angularMass;
  buffer<vec4f> rotation { capacity };
  buffer<vec4f> angularVelocity { capacity };
  buffer<vec4f> angularDerivatives[3]; // { [0 ... 2] = buffer<vec4f>(capacity) };
  Grain(const System& system) : Particle(2, 1024, Grain::mass),
   dt_angularMass(float3(system.dt/angularMass)) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<vec4f>(capacity);
  }
  buffer<vec4f> torque { capacity };
 } grain {*this};

 const vec4f dt_2 = float4(dt/2);
 void step(Grain& p, size_t i) {
  step((Particle&)p, i);
  p.rotation[i] += dt_2 * qmul(p.angularVelocity[i], p.rotation[i]);
  p.angularVelocity[i] += p.dt_angularMass * p.torque[i];
  p.rotation[i] *= rsqrt(sq4(p.rotation[i]));
 }

 struct Wire : Particle {
  using Particle::Particle;
  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  sconst float radius = 1 * mm; // 2mm diameter
  sconst float curvature = 1./radius;
  sconst float internodeLength = Grain::radius/2;
  sconst vec4f internodeLength4 = float3(internodeLength);
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1e3 * kg / cb(m);
  sconst float angularMass = 0;
  const float elasticModulus;
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float mass = Wire::density * Wire::volume;
  sconst float bendDamping = mass / s;
  const vec4f tensionStiffness = float3(elasticModulus * PI * sq(radius));
  sconst vec4f tensionDamping = float3(mass / s);
  Wire(float elasticModulus, size_t base, size_t capacity) :
    Particle{base, capacity, Wire::mass}, elasticModulus(elasticModulus) {}
 } wire;

 // Process
 const float loopAngle = PI*(3-sqrt(5.));
 sconst float winchSpeed = 1 *m/s;
 const float winchRate;
 const float initialLoad = 0.01 * kg;

 struct Parameters {
  int subStepCount;
  float frictionCoefficient;
  float wireElasticModulus;
  float winchRate;
  size_t wireCapacity;
  float initialLoad;
 };

 System(const Parameters& p) :
   subStepCount(p.subStepCount),
   frictionCoefficient(p.frictionCoefficient),
   wire {p.wireElasticModulus, grain.base+grain.capacity, p.wireCapacity},
   winchRate{p.winchRate},
   initialLoad{p.initialLoad} {}

 struct Load : Particle {
  using Particle::Particle;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e5 * kg/(m*s*s);
  vec4f torque[1];
 } load {wire.base+wire.capacity, 1, initialLoad};

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
 float kineticEnergy=0, normalEnergy=0, staticEnergy=0, tensionEnergy=0;
 float bendEnergy=0;

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB>
 void penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return;
  // Stiffness
  const float E = 1/(1/A.elasticModulus+1/B.elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  const float K = 2./3*E*sqrt(R);
  const float Ks = K * sqrt(-c.depth);
  normalEnergy += 1./2 * Ks * sq(c.depth);
  float fK = - Ks * c.depth;
  // Damping
  const float Kb = K*normalDamping*sqrt(-c.depth);
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

  vec4f relativeA = qapply(A.rotation[a], friction.localA);
  vec4f relativeB = qapply(B.rotation[b], friction.localB);
  vec4f globalA = A.position[a] + relativeA;
  vec4f globalB = B.position[b] + relativeB;
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
     ) {
   friction.lastStatic = timeStep; // Otherwise resets contact next step
   if(tangentLength[0]) {
    vec4f springDirection = tangentOffset / tangentLength;
    float fB = staticFrictionDamping * dot3(springDirection, relativeVelocity)[0];
    staticEnergy += 1./2 * kS * sq(tangentLength[0]);
    fT = - float3(fS+fB) * springDirection;
   } else fT = _0f;
  } else {
   if(tangentRelativeSpeed[0]) {
    fT = - float3(fD) * tangentRelativeVelocity / tangentRelativeSpeed;
   } else fT = _0f;
  }
  force += fT;
  A.force[a] += force;
  atomic_sub(B.force[b], force);
  A.torque[a] += cross(relativeA, fT);
  atomic_sub(B.torque[b], cross(relativeB, fT));
 }
};
constexpr vec4f System::G;
constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Floor::elasticModulus;
constexpr float System::Side::elasticModulus;
constexpr float System::Load::elasticModulus;
constexpr float System::Grain::elasticModulus;
constexpr vec4f System::Wire::tensionDamping;
