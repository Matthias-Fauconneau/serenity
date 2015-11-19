#include "window.h"
#include "matrix.h"
inline vec3 qapply(v4sf q, vec3 v) { return toVec3(qapply(q, (v4sf)v)); }
inline void closest(v4sf a1, v4sf a2, v4sf b1, v4sf b2, vec3& A, vec3& B) { v4sf a, b; closest(a1, a2, b1, b2, a, b);  A=toVec3(a), B=toVec3(b); }

struct Face {
 size_t a, b, c;
};
struct Edge {
 size_t a, b;
};

// Convex polyhedra
struct Polyhedra {
 static constexpr float boundingRadius = 1;
 static constexpr float overlapRadius = 1./16;

 const float mass = 1; // FIXME
 const float angularMass = 1; // FIXME

 vec3 position;
 v4sf rotation;
 buffer<vec3> vertices; // Vertex positions in local frame
 buffer<vec4> faces; // Face planes in local frame (normal, distance)
 buffer<Edge> edges; // Edge vertex indices
};
constexpr float Polyhedra::boundingRadius;
constexpr float Polyhedra::overlapRadius;

vec3 global(const Polyhedra& A, size_t vertexIndex) {
 vec3 laA = A.vertices[vertexIndex];
 vec3 raA = qapply(A.rotation, laA);
 vec3 gA = A.position + raA;
 return gA;
}

struct Contact {
 size_t a = invalid, b = invalid;
 size_t vertexIndexA = invalid, edgeIndexA = invalid, edgeIndexB = invalid, faceIndexB = invalid;
 vec3 rbC = 0; // Contact point relative to B position (unrotated)
 vec3 raC = 0; // Contact point relative to A position (unrotated)
 vec3 N = 0;
 float depth = -inf;
 explicit operator bool() { return depth > -inf; }
};

Contact vertexFace(const Polyhedra& A, size_t vertexIndex, const Polyhedra& B) {
 vec3 laA = A.vertices[vertexIndex];
 vec3 raA = qapply(A.rotation, laA);
 vec3 gA = A.position + raA;
 vec3 rbA = gA - B.position;
 vec3 lbA = qapply(conjugate(B.rotation), rbA); // Transforms vertex to B local frame
 // Clips against all planes (convex hull of the spheropolyhedra)
 for(vec4 plane: B.faces) if(dot(plane.xyz(), lbA) > plane.w + A.overlapRadius + B.overlapRadius) return Contact();
 /*else*/ { // P inside polyhedra B
  for(size_t faceIndex: range(B.faces.size)) {
   // Tighter clips against all other planes (convex polyhedra)
   for(size_t planeIndex: range(B.faces.size)) {
    if(planeIndex == faceIndex) continue;
    vec4 plane = B.faces[planeIndex];
    if(dot(plane.xyz(), lbA) <= plane.w) continue; // FIXME: reuse dot(P, lba) - w
    goto continue2_;
   } /*else*/ { // Contact
    vec4 bPlane = B.faces[faceIndex];
    // Projects vertex on plane (B frame)
    float t = bPlane.w - dot(bPlane.xyz(), lbA);
    assert_(t < 0); // Maximum overlap length: A.overlapRadius + B.overlapRadius
    float depth = t + A.overlapRadius + B.overlapRadius;
    assert_(depth > 0); // Implied by initial convex hull cull
    Contact contact;
    contact.vertexIndexA = vertexIndex;
    contact.faceIndexB = faceIndex;
    contact.N = qapply(B.rotation, bPlane.xyz());
    assert_(depth > 0);
    contact.depth = depth;
    vec3 lbB = lbA + t * bPlane.xyz();
    vec3 rbB = qapply(B.rotation, lbB);
    vec3 rbC = (rbA + rbB)/2.f;
    contact.rbC = rbC;
    contact.raC = rbC + B.position - A.position;
    return contact;
   }
   continue2_:;
  }
 }
 return Contact();
}

Contact edgeEdge(const Polyhedra& A, size_t eiA, const Polyhedra& B, size_t eiB) {
 Edge eA = A.edges[eiA];
 vec3 A1 = global(A, eA.a), A2 = global(A, eA.b);
 Edge eB = B.edges[eiB];
 vec3 B1 = global(B, eB.a), B2 = global(B, eB.b);
 vec3 gA, gB;
 closest(A1, A2, B1, B2, gA, gB);
 vec3 r = gA-gB;
 float L = ::length(r);
 float depth = A.overlapRadius + B.overlapRadius - L;
 if(depth > 0) {
  Contact contact;
  contact.N = r/L;
  contact.depth = depth;
  contact.edgeIndexA = eiA;
  contact.edgeIndexB = eiB;
  vec3 gC = (gA+gB)/2.f;
  contact.rbC = gC - B.position;
  contact.raC = gC - A.position;
  return contact;
 }
 return Contact();
}

#if 0 // EdgeEdge
Contact vertexEdge(const Polyhedra& A, size_t vertexIndex, const Polyhedra& B) {
 vec3 laA = A.vertices[vertexIndex];
 vec3 raA = qapply(A.rotation, laA);
 vec3 gA = A.position + raA;
 vec3 rbA = gA - B.position;
 vec3 lbA = qapply(conjugate(B.rotation), rbA); // Transforms vertex to B local frame
  for(size_t edgeIndex: range(B.edges.size)) {
   Edge e = B.edges[edgeIndex];
   vec3 P1 = B.vertices[e.a];
   vec3 P2 = B.vertices[e.b];
   vec3 E = P2 - P1;
   // Projects vertex on edge (B frame)
   float t = dot(lbA - P1, E);
   if(t <= 0 || t >= length(E)) continue;
   vec3 lbB = P1 + t * (P2 - P1);
   vec3 R = lbA-lbB;
   float L = length(R);
   float depth = A.overlapRadius + B.overlapRadius - L;
   if(depth > 0) {
    Contact contact;
    contact.vertexIndexA = vertexIndex;
    contact.edgeIndexB = edgeIndex;
    contact.N = R/L;
    contact.depth = depth;
    vec3 rbB = qapply(B.rotation, lbB);
    vec3 rbC = (rbA + rbB)/2.f;
    contact.rbC = rbC;
    contact.raC = rbC + B.position - A.position;
    return contact;
   }
  }
  return Contact();
}
#endif

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;
 buffer<vec3> velocity, angularVelocity;
 const float dt = 1./(60*16);
 const float damping = 0.1;
 const float density = 1;
 const float E = 10000;
 const vec3 g {0,0,-10};
 const float N = 2, d = pow(N, 1./3), D = d+1;
 const float frictionCoefficient = 1;//0.1;

 size_t t = 0;
 array<Contact> contacts;

 struct Force { vec3 origin, force; };
 array<Force> forces;

 tsc stepTime, contactTime, vertexFaceTime, edgeEdgeTime;

 PolyhedraSimulation(Random& random) {
  while(polyhedras.size < N) {
#if 0
   //vec3 position(d*(2*random()-1), d*(2*random()-1), d*(2*random()-1));
#else
   vec3 position (0,0,0);
   if(polyhedras.size) position = vec3(random()-1./2, random()-1./2, 1+random());
#endif
   const float r = 1./sqrt(3.);
   ref<vec3> vertices{vec3(r,r,r), vec3(r,-r,-r), vec3(-r,r,-r), vec3(-r,-r,r)};
   for(auto& p: polyhedras) {
    if(length(p.position - position) <= p.boundingRadius + Polyhedra::boundingRadius)
     goto break_;
   } /*else*/ {
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    ref<Face> faceIndices{{0,1,2}, {0,2,3}, {0,3,1}, {1,3,2}};
    buffer<vec4> faces (faceIndices.size);
    for(size_t i: range(faces.size)) {
     Face f = faceIndices[i];
     vec3 N = (vertices[f.a]+vertices[f.b]+vertices[f.c])/3.f;
     float l = length(N);
     faces[i] = vec4(N/l, l);
    }
    Polyhedra p{
     1, 1,
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices),
       move(faces),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}})
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
  float minZ = inf; for(const Polyhedra& p: polyhedras) for(vec3 v: p.vertices) minZ = ::min(minZ, p.position.z + qapply(p.rotation, v).z);
  for( Polyhedra& p: polyhedras) p.position.z -= minZ;
  velocity = buffer<vec3>(polyhedras.size);
  velocity.clear(0);
  angularVelocity = buffer<vec3>(polyhedras.size);
  angularVelocity.clear(0);
 }

 bool step() {
  stepTime.start();

  buffer<vec3> force (polyhedras.size), torque (polyhedras.size);
  array<Force> forces;

  for(size_t a: range(polyhedras.size)) {
   Polyhedra& p = polyhedras[a];
   {
    vec3 f = g * p.mass;
    forces.append(p.position, f);
    force[a] = f;
   }
   torque[a] = 0;
   vec3 N(0,0,1);
   for(size_t vertexIndex: range(polyhedras[a].vertices.size)) {
    vec3 A = p.vertices[vertexIndex];
    vec3 rA = qapply(p.rotation, A);
    vec3 gA = p.position + rA;
    float depth = 0 - gA.z;
    if(depth > 0) {
     vec3 v = velocity[a] + cross(angularVelocity[a], rA);
     float fN = 4.f/3 * E * sqrt(polyhedras[a].overlapRadius) * sqrt(depth) * (depth - damping * dot(N, v));
     vec3 tV = v - dot(N, v) * N;
     vec3 fT = - frictionCoefficient * fN * tV;
     vec3 f = fN * N + fT;
     assert_(isNumber(f));
     force[a] += f;
     torque[a] += cross(rA, f);
     //forces.append(gA, f);
    }
   }
  }

  bool status = false;
  array<Contact> contacts;
  contactTime.start();
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
    // Vertex - Face (Sphere - Plane)
    for(size_t vertexIndex: range(A.vertices.size)) {
      vertexFaceTime.start();
      Contact contact = ::vertexFace(A, vertexIndex, B);
      vertexFaceTime.stop();
      if(contact) {
       contact.a = a, contact.b = b;
       contacts.append(contact);

       vec3 N = contact.N; float depth = contact.depth;
       vec3 rV = velocity[a] + cross(angularVelocity[a], contact.raC) - (velocity[b] + cross(angularVelocity[b], contact.rbC));
       vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N;
       assert_(isNumber(f));

       force[a] += f;
       torque[a] += cross(contact.raC, f);
       forces.append(A.position + contact.raC, f);

       force[b] += -f;
       torque[b] += cross(contact.rbC, -f);
       forces.append(B.position + contact.rbC, -f);
       goto break_;
      }
    } /*else*/ {
     if(a < b) { // Edge - Edge (generalized: also Vertex-Edge and Vertex-Vertex)
      // FIXME: should be strictly "Cylinder - Cylinder" but "spheropolyhedra" does not split VE and VV cases out and always use Sphere - Sphere contacts
      // Vertex contacts will be computed as generalized Edge - Edge. Evaluating only the contact with maximum overlap ensures no duplicates
      // FIXME: Detect cap case to avoid further intersection tests (allows fast path usage) and use VE and VV overlap volumes
      Contact contact; // Approximated by maximum depth
      for(size_t eiA: range(A.edges.size)) {
       for(size_t eiB: range(B.edges.size)) {
        edgeEdgeTime.start();
        Contact candidate = ::edgeEdge(A, eiA, B, eiB);
        edgeEdgeTime.stop();
        if(candidate.depth > contact.depth) {
         contact = candidate;
        }
       }
      }
      if(contact) {
       contact.a = a, contact.b = b;
       contacts.append(contact);

       vec3 N = contact.N; float depth = contact.depth;
       vec3 rV = velocity[a] + cross(angularVelocity[a], contact.raC) - (velocity[b] + cross(angularVelocity[b], contact.rbC));
       //vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N; // Orthogonal Cylinder - Cylinder
       vec3 f = 4.f/3 * E * sqrt(1.f/2*A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N; // Sphere - Sphere (TODO: split out Cylinder - Cylinder and Sphere - Cylinder)
       assert_(isNumber(f), "EE", depth);

       force[a] += f;
       torque[a] += cross(contact.raC, f);
       forces.append(A.position + contact.raC, f);

       force[b] += -f;
       torque[b] += cross(contact.rbC, -f);
       forces.append(B.position + contact.rbC, -f);
      }
     }
    }
    break_:;
   }
  }
  contactTime.stop();

  for(size_t a: range(polyhedras.size)) {
   assert_(isNumber(force[a]));
   velocity[a] += dt * force[a];
   vec3 dx = dt * velocity[a];
   polyhedras[a].position += dx;
   assert_(isNumber(polyhedras[a].position));

   polyhedras[a].rotation += float4(dt/2.f) * qmul(angularVelocity[a], polyhedras[a].rotation);
   angularVelocity[a] += dt / polyhedras[a].angularMass * torque[a];
   polyhedras[a].rotation *= rsqrt(sq4(polyhedras[a].rotation));
  }

  {
   float maxL = 0; for(const Polyhedra& p: polyhedras) for(vec3 v: p.vertices) maxL = ::max(maxL, length(p.position + qapply(p.rotation, v)));
   if(maxL > 16) goto end;
  }

  {
    float kE = 0;
    for(size_t i: range(polyhedras.size)) {
     kE += 1./2 * polyhedras[i].mass * sq(velocity[i]);
     kE += 1./2 * polyhedras[i].angularMass * sq(angularVelocity[i]);
    }
  }
  status = true;
  end:
  this->contacts = ::move(contacts);
  this->forces = ::move(forces);
  t++;
  stepTime.stop();
  return status;
 }
};

