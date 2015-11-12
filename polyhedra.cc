#include "window.h"
#include "matrix.h"

struct Face {
 size_t a, b, c;
};
struct Edge {
 size_t a, b;
};

// Convex polyhedra
struct Polyhedra {
 vec3 position;
 v4sf rotation;
 buffer<vec3> local; // Vertex positions in local frame
 buffer<vec4> planes; // normal, distance
 buffer<Edge> edges;
 buffer<vec3> global; // Vertex positions in global frame
 float radius;
};

struct Contact {
 size_t a, b;
 /*size_t vertex;
 size_t face;
 float u, v, t;*/
 vec3 P1, P2;
 vec3 N;
 float depth;
 explicit operator bool() { return depth < inf; }
};

// Point - Plane
Contact vertexFace(const Polyhedra& A, const Polyhedra& B) {
 for(size_t vertexIndex: range(A.global.size)) {
  vec3 a = toVec3(qapply(conjugate(B.rotation), A.global[vertexIndex] - B.position)); // Transforms vertex to B local frame
  // Clips against all planes (convex polyhedra)
  for(vec4 plane: B.planes) {
   if(dot(plane.xyz(), a) <= plane.w) continue; // Inside
   goto break_;
  } /*else*/ { // P inside polyhedra B
   Contact contact {0, 0, {}, {}, {}, inf};
   for(vec4 plane: B.planes) {
    // Projects vertex on plane (B frame)
    float depth = plane.w - dot(plane.xyz(), a);
    if(depth < contact.depth) {
     contact.N = plane.xyz();
     contact.depth = depth;
     contact.P1 = A.global[vertexIndex];
     contact.P2 = B.position + toVec3(qapply(B.rotation, a + depth * plane.xyz()));
    }
   }
   return contact;
  }
  break_:;
 }
 return {0, 0, {}, {}, {}, inf};
}

// Cylinder - Cylinder
Contact edgeEdge(const Polyhedra& A, const Polyhedra& B) {
 for(Edge eA: A.edges) {
  vec3 A1 = A.global[eA.a], A2 = A.global[eA.b];
  for(Edge eB: B.edges) {
   vec3 B1 = B.global[eB.a], B2 = B.global[eB.b];
   v4sf gA, gB;
   closest(A1, A2, B1, B2, gA, gB);
   vec3 a = toVec3(qapply(conjugate(B.rotation), gA - B.position)); // Transforms global position gA to B local frame
   for(vec4 plane: B.planes) {
    if(dot(plane.xyz(), toVec3(a)) <= plane.w) continue; // Inside
    goto break_;
   } /*else*/ { // a inside polyhedra B
    vec3 b = toVec3(qapply(conjugate(A.rotation), gB - A.position)); // Transforms global position gB to A local frame
    for(vec4 plane: A.planes) {
     if(dot(plane.xyz(), toVec3(b)) <= plane.w) continue; // Inside
     //error(gA, gB, dot(plane.xyz(), toVec3(b)), plane.w);
    } /*else*/ { // b inside polyhedra A
     vec3 r = toVec3(gB-gA);
     float L = ::length(r);
     return {0,0, toVec3(gA), toVec3(gB), r/L, L};
    }
   }
   break_:;
  }
 }
 return {0, 0, {}, {}, {}, inf};
}

Contact contact(const Polyhedra& A, const Polyhedra& B) {
 return vertexFace(A, B) ?: vertexFace(B, A) ?: edgeEdge(A, B);
}

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;
 buffer<vec3> velocity;
 const float dt = 1./60;
 const float damping = 1;
 const float density = 1;
 const float E = 1;
 const float R = 1;
 const float volume = 1;
 const vec3 g {0,0,-1};

 array<Contact> contacts;

 PolyhedraSimulation() {
  Random random;
  while(polyhedras.size < 5) {
   const float d = 8;
   vec3 position(d*random(), d*random(), d*random());
   ref<vec3> vertices{vec3(1,1,1), vec3(1,-1,-1), vec3(-1,1,-1), vec3(-1,-1,1)};
   float radius = 0; for(vec3 v: vertices) radius = ::max(radius, length(v));
   for(auto& p: polyhedras) {
    if(length(p.position - position) <= p.radius + radius)
     goto break_;
   } /*else*/ {
    float t0 = 2*PI*random();
    float t1 = acos(1-2*random());
    float t2 = (PI*random()+acos(random()))/2;
    ref<Face> faces{{0,1,2}, {0,2,3}, {0,3,1}, {1,3,2}};
    buffer<vec4> planes (faces.size);
    for(size_t i: range(planes.size)) {
     Face f = faces[i];
     vec3 N = (vertices[f.a]+vertices[f.b]+vertices[f.c])/3.f;
     float l = length(N);
     planes[i] = vec4(N/l, l);
    }
    Polyhedra p{
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices),
       move(planes),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}}),
     buffer<vec3>(4), radius
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
  velocity = buffer<vec3>(polyhedras.size);
  velocity.clear(0);
 }
 bool step() {
  //vec3 center = ::mean(apply(polyhedras, [](const Polyhedra& p)->vec3{ return p.position;}));
  //bool hadContacts = contacts.size;
  contacts.clear();
  for(auto& p: polyhedras) {
   for(size_t a: range(p.local.size)) {
    p.global[a] = toVec3(p.position + qapply(p.rotation, p.local[a]));
   }
  }
  buffer<vec3> force (polyhedras.size); // TODO: torque
  for(size_t a: range(polyhedras.size)) {
   force[a] = g* density * volume;
   float depth = 0 - polyhedras[a].position.z;
   vec3 n(0,0,1);
   if(depth > 0) force[a] += 4.f/3 * E * sqrt(R) * sqrt(depth) * (depth - damping * dot(n, velocity[a])) * n;
  }
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
    Contact contact = ::contact(A, B);
    if(contact) {
     force[a] += contact.depth * contact.N;
     assert_(isNumber(force[a]), contact.depth, contact.N);
     contact.a = a, contact.b = b;
     contacts.append(contact);
    }
   }
  }
  for(size_t a: range(polyhedras.size)) {
   velocity[a] += dt * force[a];
   polyhedras[a].position += dt * velocity[a];
  }
  return true; //!contacts || !hadContacts;
 }
};


struct PolyhedraView : PolyhedraSimulation, Widget {

 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  vec2 delta = cursor-lastPos; lastPos=cursor;
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
  }
  else return false;
  return true;
 }

 vec2 sizeHint(vec2) override { return 1024; }
 shared<Graphics> graphics(vec2 size) override {
  shared<Graphics> graphics;

  vec3 center = ::mean(apply(polyhedras, [](const Polyhedra& p)->vec3{ return p.position;}));

  v4sf viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  // Transforms vertices and evaluates scene bounds
  vec3 min = -5/*inf*/, max = 5/*-inf*/;
  for(auto& p: polyhedras) {
   for(vec3 a: p.global) {
    vec3 A = toVec3(qapply(viewRotation, a - center));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }

  vec2 scale = size/(max-min).xy();
  scale.x = scale.y = ::min(scale.x, scale.y);
  vec2 offset = - scale * min.xy();

  for(const Polyhedra& A: polyhedras) {
   for(Edge eA: A.edges) {
    vec3 A1 = A.global[eA.a], A2 = A.global[eA.b];
    {
     vec2 P1 = offset + scale * toVec3(qapply(viewRotation, A1 - center)).xy();
     vec2 P2 = offset + scale * toVec3(qapply(viewRotation, A2 - center)).xy();
     graphics->lines.append(P1, P2);
    }
    if(0) for(const Polyhedra& B: polyhedras) {
     if(&A == &B) continue;
     for(Edge eB: B.edges) {
      vec3 B1 = B.global[eB.a], B2 = B.global[eB.b];
      v4sf gA, gB;
      closest(A1, A2, B1, B2, gA, gB);
      vec2 P1 = offset + scale * toVec3(qapply(viewRotation, gA - center)).xy();
      vec2 P2 = offset + scale * toVec3(qapply(viewRotation, gB - center)).xy();
      graphics->lines.append(P1, P2);
      break;
     }
    }
   }
  }
  for(Contact& contact: contacts) {
   vec2 P1 = offset + scale * toVec3(qapply(viewRotation, contact.P1 - center)).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, contact.P2 - center)).xy();
   graphics->lines.append(P1, P2);
   }
  return graphics;
 }
};

struct PolyhedraApp : PolyhedraView {
 unique<Window> window = ::window(this);
 PolyhedraApp() {
  step();
  window->presentComplete = [this]{ if(step()) window->render(); };
 }
} polyhedra;
