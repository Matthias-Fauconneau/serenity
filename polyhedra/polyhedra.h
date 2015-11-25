#include "window.h"
#include "matrix.h"
inline vec3 qapply(v4sf q, vec3 v) { return toVec3(qapply(q, (v4sf)v)); }
inline void closest(v4sf a1, v4sf a2, v4sf b1, v4sf b2, vec3& A, vec3& B) { v4sf a, b; closest(a1, a2, b1, b2, a, b);  A=toVec3(a), B=toVec3(b); }

/*struct Face {
 static constexpr size_t maxDegree = 3; // \/ 3 faces for every vertex (FIXME: Allow polyhedras with arbitrary degree at vertices | Higher max (duplicate face indices))
 size_t a, b, c; // (FIXME: Allow polyhedras with arbitrary degree at faces | Higher max (duplicate vertex indices))
 operator ref<size_t>() const { return {&a, maxDegree}; }
};*/
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
 static constexpr size_t maxVertexDegree = 3; // \/ 3 edges for every vertex (FIXME: Allow polyhedras with arbitrary degree at vertices | Higher max (duplicate indices))
 buffer<size_t> vertexToFace;
 static constexpr size_t maxFaceDegree = 3; // \/ 3 edges for every face (FIXME: Allow polyhedras with arbitrary degree at faces | Higher max (duplicate indices) | Triangulate)
 buffer<size_t> faceToFace;
 buffer<Edge> edges; // Edge vertex indices
};
constexpr float Polyhedra::boundingRadius;
constexpr float Polyhedra::overlapRadius;

struct Contact {
 size_t a = invalid, b = invalid;
 size_t vertexIndexA = invalid, edgeIndexA = invalid, edgeIndexB = invalid, faceIndexB = invalid;
 vec3 rbC = 0; // Contact point relative to B position (unrotated)
 vec3 raC = 0; // Contact point relative to A position (unrotated)
 vec3 N = 0;
 float depth = -inf;
 explicit operator bool() { return depth > -inf; }
};

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

 array<vec4> planes; // CP visualization

 tsc stepTime, contactTime, vertexFaceTime, edgeEdgeTime;

 PolyhedraSimulation(Random& random) {
  while(polyhedras.size < N) {
   vec3 position;
   if(N==2) position = polyhedras.size==0 ? vec3(0,0,0) : vec3(random()-1./2, random()-1./2, 1+random());
   else position = vec3(d*(2*random()-1), d*(2*random()-1), d*(2*random()-1));

   const float r = 1./sqrt(3.);
   // TODO: Asserts origin is centroid
   for(auto& p: polyhedras) {
    if(length(p.position - position) <= p.boundingRadius + Polyhedra::boundingRadius)
     goto break_;
   } /*else*/ {
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    ref<Edge> edgesVertexIndices{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}};
    ref<Face> faceVertexIndices {{0,1,2}, {0,2,3}, {0,3,1}, {1,3,2}}; // FIXME: generate from edge list
    ref<vec3> vertices{vec3(r,r,r), vec3(r,-r,-r), vec3(-r,r,-r), vec3(-r,-r,r)};
    buffer<vec4> faces (faceVertexIndices.size);
    for(size_t i: range(faces.size)) {
     Face f = faceVertexIndices[i];
     vec3 N = (vertices[f.a]+vertices[f.b]+vertices[f.c])/3.f;
     float l = length(N);
     faces[i] = vec4(N/l, l);
    }
    // Vertex to Face
    buffer<size_t> vertexToFace(vertices.size*Polyhedra::maxVertexDegree);
    vertexToFace.clear(invalid);
    for(size_t faceIndex: range(faceVertexIndices.size)) {
     for(size_t vertexIndex: (ref<size_t>)faceVertexIndices[faceIndex]) {
      size_t i = 0;
      while(vertexToFace[vertexIndex*Polyhedra::maxVertexDegree+i]!=invalid) {
       i++;
       assert_(i < Polyhedra::maxVertexDegree, vertexToFace, vertexIndex, faceIndex);
      }
      vertexToFace[vertexIndex*Polyhedra::maxVertexDegree+i] = faceIndex;
    }
    }
    // Face to Face
    buffer<size_t> faceToFace(vertices.size*Polyhedra::maxFaceDegree);
    faceToFace.clear(invalid);
    for(Edge e: edgesVertexIndices) {
     for(size_t vertexIndex: (ref<size_t>)faceVertexIndices[faceIndex]) {
      size_t i = 0;
      while(faceToFace[vertexIndex*Polyhedra::maxVertexDegree+i]!=invalid) {
       i++;
       assert_(i < Polyhedra::maxVertexDegree, faceToFace, vertexIndex, faceIndex);
      }
      faceToFace[vertexIndex*Polyhedra::maxVertexDegree+i] = faceIndex;
    }
    }
    error(faceToFace);
    Polyhedra p{
     1, 1,
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices), move(faces), move(vertexToFace), move(faceToFace), copyRef(edgesVertexIndices)
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
  array<vec4> planes;
  contactTime.start();
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
#if 0
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
#else
    // Initial contact plane as perpendicular bisector plane of centroids
    vec3 AB = B.position - A.position; // Assumes origin is centroid
    vec3 N = AB/length(AB);
    float d = dot(N, (A.position + B.position)/2.f);
    planes.append(N, d);
    vec3 PQ[2]; size_t index[2];
    {float maxD = -inf;
     for(size_t vertexIndex : range(A.vertices.size)) {
      vec3 gA = A.position +  qapply(A.rotation, A.vertices[vertexIndex]);
      float d = dot(N, gA);
      if(d > maxD) { maxD=d; index[0] = vertexIndex; PQ[0]=gA;}
     }
    }
    {float minD = inf;
     for(size_t vertexIndex : range(B.vertices.size)) {
      vec3 gB = B.position +  qapply(B.rotation, B.vertices[vertexIndex]);
      float d = dot(N, gB);
      if(d < minD) { minD=d; index[1] = vertexIndex; PQ[1]=gB; }
     }
    }
    enum { Vertex, Edge, Face } type[2] = {Face, Face};
    // Shortest link method iterations
    for(;;) {
     const Polyhedra* AB[2] = {&A, &B};
     for(size_t i : range(2)) { // Optimizes distance to contact plane alternating between point on A/B
      // Variables are named as in the i=0 case, i.e Finds closest point on A to Q (on B) starting from P (on A)
      const Polyhedra& A = AB[i], B = AB[!i]; // A and B alternates roles
      vec3 P = PQ[i], Q = PQ[!i]; // P and Q alternates roles
      if(type[i] == Vertex) { // Finds closest point to Q on the 3 adjacent faces of P
       size_t pVertexIndex = index[i];
       for(size_t AfaceIndex: A.vertexToFace.slice(pVertexIndex*Polyhedra::maxVertexDegree, Polyhedra::maxVertexDegree)) {
        vec4 face = A.faces[AfaceIndex];
        vec3 N = face.xyz(); float d = face.w;
        vec3 Pc = Q - (dot(N, Q) - d) * N; // Projects Q on candidate face

       }
      }
     }
    }
#endif
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
  this->planes = ::move(planes);
  t++;
  stepTime.stop();
  return status;
 }
};

