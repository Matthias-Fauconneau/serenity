#include "simd.h"
#include "parallel.h"
#include "variant.h"
#include "matrix.h"
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
 const float dt;
 const vec4f b[4] = {float3(dt), float3(dt*dt/2), float3(dt*dt*dt/6), float3(dt*dt*dt*dt/24)};
 const vec4f c[5] = {float3(19./90), float3(3/(4*dt)), float3(2/(dt*dt)), float3(6/(2*dt*dt*dt)),
                                float3(24/(12*dt*dt*dt*dt))};

 // SI units
 sconst float s = 1, m = 1, kg = 1, N = kg*m/s/s;
 sconst float mm = 1e-3*m, g = 1e-3*kg;
 vec4f G {0, 0, -10 * N/kg/*=m·s¯²*/, 0};

 // Penalty model
 sconst float normalDamping = 0 * g/s;
 // Friction model
 sconst float staticFrictionSpeed = 1./3 *m/s;
 sconst float staticFrictionFactor = 1/(2e-3 *m); // Wire diameter
 sconst float staticFrictionLength = 10 *mm;
 sconst float staticFrictionDamping = 15 *g/s/s;
 const float frictionCoefficient;

 struct Contact {
  vec4f relativeA, relativeB; // Relative to center but world axices
  vec4f normal;
  float depth;
  float u, v; // FIXME: barycentric coordinates passed from contact to force distribution
 };

 /// Returns contact between two objects
 /// Defaults implementation for spheres
 template<Type tA, Type tB>
 Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
  vec4f relativePosition = A.position[a] - B.position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f normal = relativePosition/length; // B -> A
  return {float3(tA::radius)*(-normal), float3(tB::radius)*(+normal),
     normal, length[0]-tA::radius-tB::radius,0,0};
 }

 struct Friction {
  size_t index; // Identifies contact to avoid duplicates
  vec4f localA, localB; // Last static friction contact location
  size_t lastStatic = 0;
  float energy;
  bool operator==(const Friction& b) const { return index == b.index; }
 };

 struct Plate {
  sconst bool friction = true;
  sconst float mass = inf;
  sconst float angularMass = 0;
  sconst size_t count = 2;
  vec4f position[2] = {_0f, _0f};
  struct { vec4f operator[](size_t){ return _0f; }} velocity;
  struct { vec4f operator[](size_t){ return _0001f; }} rotation;
  struct { vec4f operator[](size_t){ return _0f; }} angularVelocity;
  vec4f force[2];
  vec4f torque[2];
  sconst size_t base = 0;
  sconst float curvature = 0;
  sconst float elasticModulus = 1e9 * kg/(m*s*s); // 1 GPa
 } plate;
 /// Sphere - Plate
 template<Type tA>
 Contact contact(const tA& A, size_t a, const Plate& B, size_t b) {
  if(b == 0) { // Bottom
   vec4f normal {0, 0, 1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.position[a][0], A.position[a][1], B.position[b][2], 0},
      normal, (A.position[a][2]-tA::radius) - B.position[b][2],0,0 };
  } else { // Top
   vec4f normal {0, 0, -1, 0};
   return { float3(tA::radius)*(-normal),
      vec4f{A.position[a][0], A.position[a][1], B.position[b][2], 0},
      normal, B.position[b][2] - (A.position[a][2]+tA::radius),0,0 };
  }
 }

 struct Vertex {
  sconst bool friction = true;
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

  Vertex(size_t base, size_t capacity, float mass) : base(base), capacity(capacity),
    mass(mass), _1_mass(float3(1./mass)) {
   for(size_t i: range(3)) positionDerivatives[i] = buffer<vec4f>(capacity);
   frictions.clear();
  }
 };

 void step(Vertex& p, size_t i) {
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

 struct Grain : Vertex {
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
  Grain(const System& system) : Vertex(2, 32768, Grain::mass), // 5 MB
   dt_angularMass(float3(system.dt/angularMass)) {
   for(size_t i: range(3)) angularDerivatives[i] = buffer<vec4f>(capacity);
  }
  buffer<vec4f> torque { capacity };
 } grain {*this};

 const vec4f dt_2 = float4(dt/2);
 void step(Grain& p, size_t i) {
  step((Vertex&)p, i);
  p.rotation[i] += dt_2 * qmul(p.angularVelocity[i], p.rotation[i]);
  p.angularVelocity[i] += p.dt_angularMass * p.torque[i];
  p.rotation[i] *= rsqrt(sq4(p.rotation[i]));
 }

 struct Wire : Vertex {
  using Vertex::Vertex;
  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  sconst float radius = 1 * mm; // 2mm diameter
  sconst float curvature = 1./radius;
  sconst float internodeLength = Grain::radius/2;
  sconst vec4f internodeLength4 = float3(internodeLength);
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1e3 * kg / cb(m);
  sconst float angularMass = 0;
  const float elasticModulus; // ~ 8e6
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float mass = Wire::density * Wire::volume;
  sconst float bendDamping = mass / s;
  const vec4f tensionStiffness = float3(elasticModulus * PI * sq(radius));
  sconst vec4f tensionDamping = float3(mass / s);
  float tensionEnergy=0;
  Wire(float elasticModulus, size_t base) :
    Vertex{base, 32768, Wire::mass}, elasticModulus(elasticModulus) {}
 } wire;

 struct Side : Vertex {
  sconst bool friction = false;

  sconst float curvature = 0; // -1/radius?
  sconst float elasticModulus = 1e6 * kg/(m*s*s);
  sconst float thickness = 1*mm;
  sconst float resolution = Grain::radius/2;
  const float initialRadius;
  const float height;
  const size_t W, H;
  size_t faceCount = 1;
  //float minRadius = initialRadius;

  const float internodeLength = 2*PI*initialRadius/W;
  const vec4f internodeLength4 = float3(internodeLength);

  const vec4f tensionStiffness = float3(elasticModulus * internodeLength/3*thickness); // FIXME
  const vec4f tensionDamping = float3(mass / s);
  sconst float areaMomentOfInertia = pow4(1*mm); // FIXME
  const float bendStiffness = elasticModulus * areaMomentOfInertia / internodeLength; // FIXME

  float tensionEnergy=0;

  /*struct { v4sf operator[](size_t) const { return _0f; } } position;

  struct {
   System::Side& side;
   // FIXME: pass contact barycentric coordinates to evaluate velocity
   v4sf operator[](size_t unused faceIndex) const { return _0f; }
  } velocity {*this};

  struct DistributeForce {
   System::Side& side;
   size_t faceIndex;
  };
  struct {
   System::Side& side;
   DistributeForce operator[](size_t faceIndex) const { return {side, faceIndex}; }
  } force {*this};*/

  struct { NoOperation operator[](size_t) const { return {}; }} torque;

  Side(float initialRadius, float height, size_t base)
   : Vertex(base, /*W*H*/int(2*PI*initialRadius/resolution)*
                                         (int(height/resolution*2/sqrt(3.))+1), 8*g),
     initialRadius(initialRadius), height(height),
     W(int(2*PI*initialRadius/resolution)),
     H(int(height/resolution*2/sqrt(3.))+1) {
   count = W*H;
   for(size_t i: range(H)) for(size_t j: range(W)) {
    float z = i*height/(H-1);
    float a = 2*PI*(j+(i%2)*1./2)/W;
    float x = initialRadius*cos(a), y = initialRadius*sin(a);
    Vertex::position[i*W+j] = {x,y,z,0};
   }
   Vertex::velocity.clear(_0f);
   for(size_t i: range(3)) positionDerivatives[i].clear(_0f);
  }
 } side;
 struct Debug { size_t index; float u, v; };
 array<Debug> flag, flag2;
 /// Sphere - Side
 template<Type tA>
 Contact contact(const tA& A, size_t aIndex, const Side& side, size_t faceIndex) {
  {float length = length2(A.position[aIndex]);
   //if(length+Grain::radius < side.minRadius) return {_0f,_0f,_0f,0,0,0}; // Quick cull
   if(side.faceCount == 1) { // Side -> Sphere
    float r = side.initialRadius;
    vec4f normal {-A.position[aIndex][0]/length, -A.position[aIndex][1]/length, 0, 0};
    return { float3(tA::radius)*(-normal),
       vec4f{r*-normal[0], r*-normal[1], A.position[aIndex][2], 0},
     normal, r-tA::radius-length,0,0 };
   }
  }
#if 1
  vec4f b = side.Vertex::position[faceIndex];
  vec4f relativePosition = A.position[aIndex] - b;
  vec4f length = sqrt(sq3(relativePosition));
  assert_(length[0]);
  vec4f normal = relativePosition/length; // B -> A
  //if(length[0]-tA::radius<0) log(A.position[aIndex], b, relativePosition, length, normal);
  return {float3(tA::radius)*(-normal), _0f/*b*/, normal, length[0]-tA::radius, 0, 0};
#else
  size_t W = side.W;
  size_t i = faceIndex/2/W, j = (faceIndex/2)%W;
  vec3 a (toVec3(side.Vertex::position[i*W+j]));
  vec3 b (toVec3(side.Vertex::position[i*W+(j+1)%W]));
  vec3 c (toVec3(side.Vertex::position[(i+1)*W+j]));
  vec3 d (toVec3(side.Vertex::position[(i+1)*W+(j+1)%W]));
  vec3 vertices[2][2][3] {{{a,c,b},{b,c,d}},{{a,c,d},{a,d,b}}};
  ref<vec3> V (vertices[i%2][faceIndex%2], 3);
#if 1
  // Triangle - Sphere
  vec3 O = toVec3(A.position[aIndex]);
  vec3 E1 = V[1]-V[0], E2 = V[2]-V[0];
  vec3 D = -cross(E1, E2);
  D /= length(D);
  vec3 P = cross(D, E2);
  float det = dot(P, E1);
  if(det < 0.000001) { error(i%2, faceIndex%2); return {_0f,_0f,_0f,0,0,0}; }
  vec3 T = O - V[0];
  float invDet = 1 / det;
  float u = invDet * dot(P, T);
  //u = max(0, u);
  Contact contact {_0f,_0f,_0f,inf,0,0};
  if(!(u < 0 || u > 1)) {
   vec3 Q = cross(T, E1);
   float v = invDet * dot(Q, D);
   //v = max(0, v);
   if(!(v < 0 || u + v  > 1)) {
    float t = dot(Q, E2) * invDet;
    //if(t<0) return {_0f,_0f,_0f,0,0,0};
    if(t < tA::radius) flag.append({faceIndex,u,v});
    contact = {float3(tA::radius)*D, O+t*D, V[0]+/*-*/D, t-tA::radius, u, v};
   }
  }
#else
  Contact contact {_0f,_0f,_0f, inf,0,0};
#endif
  // Point - Sphere
  for(vec3 v: V) { // FIXME: test vertices once (6x overhead)
   vec4f relativePosition = A.position[aIndex] - v;
   vec4f length = sqrt(sq3(relativePosition));
   if(length[0]-tA::radius < contact.depth) {
    vec4f normal = relativePosition/length; // B -> A
    contact = {float3(tA::radius)*(-normal), v/*-V[0]*/, normal, length[0]-tA::radius, 0, 0};
   }
  }
  return contact;
#endif
 }

 System(const Dict& p) :
   dt(p.at("Time step"_)),
   frictionCoefficient(p.at("Friction"_)),
   wire(p.at("Elasticity"_), grain.base+grain.capacity),
   side(p.at("Radius"_), p.at("Height"_), wire.base+wire.capacity) {}

 // Update
 size_t timeStep = 0;
 float grainKineticEnergy=0, wireKineticEnergy=0, normalEnergy=0,
         staticEnergy=0, bendEnergy=0;
 bool recordContacts = false;
 struct ContactForce { size_t a, b; vec3 relativeA, relativeB; vec3 force; };
 array<ContactForce> contacts, contacts2;

 /// Evaluates contact penalty between two objects
 template<Type tA, Type tB>
 bool penalty(const tA& A, size_t a, tB& B, size_t b) {
  Contact c = contact(A, a, B, b);
  if(c.depth >= 0) return false;
  // Stiffness
  const float E = 1/(1/A.elasticModulus+1/B.elasticModulus);
  constexpr float R = 1/(tA::curvature+tB::curvature);
  const float K = 4./3*E*sqrt(R);
  const float Ks = K * sqrt(-c.depth);
  atomic_add(normalEnergy, 1./2 * Ks * sq(c.depth));
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

  if(B.friction) {
   Friction& friction = A.frictions[a].add(Friction{B.base+b, _0f, _0f, 0, 0.f});
   if(friction.lastStatic < timeStep-1) { // Resets contact (TODO: GC)
    c.relativeA[3] = 0; // FIXME
    c.relativeB[3] = 0; // FIXME
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
   friction.energy = 0;
   if(fS < fD && tangentLength[0] < staticFrictionLength
      && tangentRelativeSpeed[0] < staticFrictionSpeed
      ) {
    friction.lastStatic = timeStep; // Otherwise resets contact next step
    if(tangentLength[0]) {
     vec4f springDirection = tangentOffset / tangentLength;
     float fB = staticFrictionDamping * dot3(springDirection, relativeVelocity)[0];
     atomic_add(staticEnergy, 1./2 * kS * sq(tangentLength[0]));
     friction.energy = 1./2 * kS * sq(tangentLength[0]);
     fT = - float3(fS+fB) * springDirection;
    } else fT = _0f;
   } else {
    if(tangentRelativeSpeed[0]) {
     fT = - float3(fD) * tangentRelativeVelocity / tangentRelativeSpeed;
    } else fT = _0f;
   }
   force += fT;
   A.torque[a] += cross(relativeA, fT);
   atomic_sub(B.torque[b], cross(relativeB, fT));
  }
  A.force[a] += force;
  atomic_sub(B.force[b], force);
  if(recordContacts) {
   contacts.append(A.base+a, B.base+b, toVec3(c.relativeA), toVec3(c.relativeB), toVec3(force));
  }
  return true;
 }
};

/*void atomic_sub(System::Side::DistributeForce unused o, vec4f unused force) {
 size_t W = o.side.W;
 size_t i = o.faceIndex/2/W, j = (o.faceIndex/2)%W, n = o.faceIndex%2;
 size_t a = i*W+j;
 size_t b = i*W+(j+1)%W;
 size_t c = (i+1)*W+j;
 size_t d = (i+1)*W+(j+1)%W;
 size_t vertices[2][2][3] {{{a,c,b},{b,c,d}},{{a,c,d},{a,d,b}}};
 // FIXME: distribute using contact barycentric coordinates
 for(size_t index: vertices[i%2][n]) atomic_sub(o.side.Vertex::force[index], force/float4(3));
}*/

constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr vec4f System::Wire::internodeLength4;
constexpr float System::Plate::elasticModulus;
constexpr float System::Side::elasticModulus;
constexpr float System::Grain::elasticModulus;
constexpr vec4f System::Wire::tensionDamping;
