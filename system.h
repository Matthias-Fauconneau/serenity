#include "vector.h"

struct System {
#define sconst static constexpr
 sconst bool rollTest = 0;
 sconst bool useWire = 0 && !rollTest;
 sconst bool useRotation = 0;
 sconst bool useFriction = 1;

 // Characteristic dimensions (SI)
 sconst real T = 1 /* 1 s */, L = 1 /* 1 m */, M = 1 /* 1 kg */;
 sconst real loopAngle = 0 ? (0 ? PI*(3-sqrt(5.)) : (2*PI)/5) : 0;
 sconst real staticFrictionSpeed = 1 *L/T;
 sconst real staticFrictionFactor = 1; //0x1p5 /L;
 sconst real staticFrictionDamping = 0x1p-6 * M/T/T;
 sconst real dynamicFriction = 1./16 *M/T;
 sconst real dampingFactor = 1./3 /  1e-2; // /√R
 sconst real wireBendStiffness = 0;//0x1p-12;
 sconst real wireBendDamping = 0x1p-12;
 sconst real winchRate = 128 /T;
 sconst real winchSpeed = 1./6 *L/T;
 sconst vec3 g {0, 0, -10}; // N/kg = m·s¯²
 sconst int subStepCount = 8;
 sconst real dt = 1./(60*subStepCount*4*(rollTest?4:1));
 #define DBG_FRICTION 0

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
  vec3f localA = 0, localB = 0; // Last static friction contact location
  size_t lastUpdate = 0;
  bool operator==(const Friction& b) const {
   return index == b.index;
  }
 };

 struct Obstacle {
  sconst real mass = inf;
  sconst real angularMass = 0;
  sconst size_t count = 1;
  struct { vec3f operator[](size_t){ return 0; }} position;
  struct { vec3f operator[](size_t){ return 0; }} velocity;
  struct { quat operator[](size_t){ return quat(); }} rotation;
  struct { vec3f operator[](size_t){ return 0; }} angularVelocity;
  vec3f force[1];
 };

 struct Floor : Obstacle {
  sconst size_t base = 0;
  sconst real radius = inf;
  sconst real height = 0; //8*L/256; //Wire::radius;
  sconst real curvature = 0;
  sconst real elasticModulus = 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;
  sconst real staticFrictionThresholdSpeed = staticFrictionSpeed;
 } floor;
 /// Sphere - Floor
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Floor&, size_t) {
  vec3f normal (0, 0, 1);
  return { float(tA::radius)*(-normal),
               vec3f(A.position[a].xy(), Floor::height),
               normal, A.position[a].z-tA::radius-Floor::height };
 }

 struct Side : Obstacle {
  sconst size_t base = 1;
  sconst real curvature = 0; // -1/radius?
  sconst real elasticModulus = 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M/T;
  sconst real frictionCoefficient = 1;
  real castRadius = Grain::radius*10;
  struct {
   real radialSum;
   auto operator[](size_t){ return *this; }
   void operator -=(vec3f f) { radialSum += length(f.xy()); }
  } force;
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

 struct Particle {
  const size_t base, capacity;
  const real mass;
  buffer<vec3f> position { capacity };
  buffer<vec3f> velocity { capacity };
  buffer<vec3f> positionDerivatives[3]; // { [0 ... 2] = buffer<vec3f>(capacity) };
  buffer<vec3f> force { capacity };
  buffer<array<Friction>> frictions { capacity };
  size_t count = 0;

  struct { quat operator[](size_t) const { return quat(); }} rotation;
  struct { vec3f operator[](size_t) const { return 0; }} angularVelocity;

  Particle(size_t base, size_t capacity, real mass) : base(base), capacity(capacity),
    mass(mass) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<vec3f>(capacity);
   frictions.clear();
  }
  sconst real b[] = {dt, dt*dt/2, dt*dt*dt/6, dt*dt*dt*dt/24};
  sconst real c[] = {19./90, 3/(4*dt), 2/(dt*dt), 6/(2*dt*dt*dt), 24/(12*dt*dt*dt*dt)};
  void step(size_t i) {
   assert(isNumber(position[i]) && isNumber(velocity[i]) && isNumber(force[i]),
          i, position[i], velocity[i], force[i]
          );
   /*log(i, position[i], velocity[i], force[i], positionDerivatives[0][i],
     positionDerivatives[1][i], positionDerivatives[2][i]);*/
   // Correction
   vec3f r = (dt*dt)/2 * (force[i] / mass - positionDerivatives[0][i]);
   /*assert(isNumber(position[i]) && isNumber(velocity[i]) && isNumber(force[i]), "|", i, position[i], velocity[i], force[i], r, force[i] / mass, force[i] / mass - positionDerivatives[0][i],
     (dt*dt)/2, (dt*dt)/2 * (force[i] / mass - positionDerivatives[0][i]), r, capacity);*/

   position[i] += c[0]*r;
   velocity[i] += c[1]*r;
   for(size_t n: range(3)) positionDerivatives[n][i] += c[2+n]*r;

   // Prediction
   position[i] += b[0]*velocity[i];
   for(size_t n: range(3)) {
    position[i] += b[1+n]*positionDerivatives[n][i];
    velocity[i] += b[n]*positionDerivatives[n][i];
    for(size_t j: range(3-(n+1)))
     positionDerivatives[n][i] += b[j]*positionDerivatives[n+1+j][i];
   }
   assert(isNumber(position[i]) && isNumber(velocity[i]) && isNumber(force[i]),">",
           i, position[i], velocity[i], force[i]);
  }
 };

 struct Grain : Particle {
  sconst size_t base = 2;

  // Properties
  sconst real radius = 2e-2; // 40 mm diameter
  sconst real curvature = 1./radius;
  sconst real elasticModulus = 1e6 * M / (L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;

  const float angularMass = 2./5*mass*(pow5(radius)-pow5(radius-1e-4))
                                         / (cb(radius)-cb(radius-1e-4));
  buffer<quat> rotation { capacity };
  buffer<vec3f> angularVelocity { capacity };
  buffer<vec3f> angularDerivatives[3];// { [0 ... 2] = buffer<vec3f>(capacity) };
  Grain(size_t base, size_t capacity) : Particle(base, capacity, 3e-3) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<vec3f>(capacity);
  }
  buffer<vec3f> torque { capacity };

  void step(size_t i) {
   Particle::step(i);
   // Correction
   vec3 r = (dt*dt)/2 * (torque[i]  / angularMass - angularDerivatives[0][i]);
   rotation[i] += rotation[i] * quat{0, c[0]*r};
   angularVelocity[i] += c[1]*r;
   for(size_t n: range(3)) angularDerivatives[n][i] += c[2+n]*r;

   // Prediction
   rotation[i] += rotation[i]*quat{0, dt*angularVelocity[i]};
   for(size_t n: range(3)) {
    rotation[i] += rotation[i]*quat{0, b[1+n]*angularDerivatives[n][i]};
    angularVelocity[i] += b[n]*angularDerivatives[n][i];
    for(size_t j: range(3-(n+1)))
     angularDerivatives[n][i] += b[j]*angularDerivatives[n+1+j][i];
   }
   rotation[i]=normalize(rotation[i]);
  }
 } grain {0, rollTest ? 2 : 1024};

 struct Wire : Particle {
  using Particle::Particle;
  sconst real radius = 1e-3; // 2mm diameter
  sconst real curvature = 1./radius;
  sconst real internodeLength = 1./2 * Grain::radius;
  sconst real volume = PI * sq(radius) * internodeLength;
  sconst real density = 1e3 * M / cb(L);
  sconst real angularMass = 0;
  sconst real elasticModulus = 32 * 1e6 * M / (L*T*T); // 1e6
  sconst real normalDamping = dampingFactor * M / T; // 0.3
  sconst real frictionCoefficient = 1;

  sconst real tensionStiffness = elasticModulus*PI*sq(radius);
  const real tensionDamping = 0x1p-8 * mass/T;
 } wire {grain.base+grain.capacity, useWire ? 5*1024 : 0,
         Wire::density * Wire::volume};

 struct Load : Particle {
  using Particle::Particle;
  sconst real curvature = 0;
  sconst real elasticModulus = 1e6 * M/(L*T*T);
  sconst real normalDamping = dampingFactor * M / T;
  sconst real frictionCoefficient = 1;
 } load {wire.base+wire.capacity, 1, 1./8 * M};
 /// Sphere - Load
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Load& B, size_t) {
  vec3f normal (0, 0, -1);
  return { float(tA::radius)*(-normal),
               vec3f(A.position[a].xy(), B.position[0].z),
               normal, B.position[0].z-(A.position[a].z+tA::radius) };
 }

 // Update
 size_t timeStep = 0;
 size_t staticFrictionCount = 0, dynamicFrictionCount = 0;
 real potentialEnergy = 0;

 /// Evaluates contact penalty between two objects
 /// \return torque on A
 template<Type tA, Type tB>
 vec3f penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  assert_(isNumber(c.depth), A.position[a], B.position[b]);
  if(c.depth >= 0) return 0;
  // Stiffness
  constexpr real E= 1/(1/tA::elasticModulus+1/tB::elasticModulus);
  constexpr real R = 1/(tA::curvature+tB::curvature);
  const real K = 2./3*E*sqrt(R);
  const real Ks = sqrt(-c.depth); // ! f(x) ~ linear approximation
  potentialEnergy += 1./2 * Ks * sq(c.depth);
  real fK = - Ks * c.depth;
  // Damping
  const real Kb = K*(tA::normalDamping+tB::normalDamping)/2*sqrt(-c.depth);
  vec3f relativeVelocity =
      A.velocity[a] + cross(A.angularVelocity[a], c.relativeA);
   - (B.velocity[b] + cross(B.angularVelocity[b], c.relativeB));
  real normalSpeed = dot(c.normal, relativeVelocity);
  real fB = - Kb * normalSpeed ; // Damping
  real fN = fK + fB;
  {
   vec3f f = fN * c.normal;
   A.force[a] += f;
   B.force[b] -= f;
  }
  if(!useFriction) return 0;
  vec3f localA = A.rotation[a].conjugate()*c.relativeA;
  vec3f localB = B.rotation[b].conjugate()*c.relativeB;
  Friction& f = A.frictions[a].add(Friction{B.base+b});
  if(f.lastUpdate < timeStep-1) { // Resets contact (TODO: GC)
   f.localA = localA;
   f.localB = localB;
  }
  f.lastUpdate = timeStep;

  constexpr real frictionCoefficient
    = min(tA::frictionCoefficient, tB::frictionCoefficient);
  vec3f relativeA = A.rotation[a] * f.localA;
  vec3f relativeB = B.rotation[b] * f.localB;
  vec3f globalA = A.position[a] + relativeA;
  vec3f globalB = B.position[b] +  relativeB;
  vec3f x = globalA - globalB;
  vec3f tangentOffset = x - dot(c.normal, x) * c.normal;
  float tangentLength = ::length(tangentOffset);
  real staticFrictionStiffness = staticFrictionFactor * frictionCoefficient;
  real kS = staticFrictionStiffness * fN;
  real fS = kS * tangentLength; // 0.1~1 fN

  real fD = frictionCoefficient * fN;
  vec3f fT;
  assert_(isNumber(fS) && isNumber(fD), fS, fD, fN, fK, fB, c.depth);
  if(fS < fD) {
   if(!tangentLength) return 0;
   vec3f springDirection = tangentOffset / tangentLength;
   real fB = staticFrictionDamping * dot(springDirection, relativeVelocity);
   fT = - (fS+fB) * springDirection;
   staticFrictionCount++;
  } else {
   vec3f tangentRelativeVelocity
     = relativeVelocity - dot(c.normal, relativeVelocity) * c.normal;
   float tangentRelativeSpeed = ::length(tangentRelativeVelocity);
   if(!tangentRelativeSpeed) return 0;
   fT = fD * tangentRelativeVelocity / tangentRelativeSpeed;
   dynamicFrictionCount++;
  }
  assert_(isNumber(fT), fS, fD, fN, fK, fB, c.depth, fT);
  A.force[a] += fT;
  B.force[b] -= fT;
  return cross(relativeA, fT);
 }
};
constexpr real System::Particle::b[];
constexpr real System::Particle::c[];
constexpr real System::Grain::radius;
constexpr vec3f System::g;
