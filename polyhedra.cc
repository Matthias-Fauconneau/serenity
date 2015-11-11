#include "window.h"
#include "matrix.h"

struct Face {
 size_t a, b, c;
};
struct Edge {
 size_t a, b;
};

// Convex triangle faces polyhedra
struct Polyhedra {
 vec3 position;
 v4sf rotation;
 buffer<vec3> local; // Vertex positions in local frame
 buffer<Face> faces;
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
 float t;
 //explicit operator bool() { return face != invalid; }
};

// Sphere - Triangle
Contact vertexFace(const Polyhedra& A, const Polyhedra& B, float R = 0) {
 for(size_t vertexIndex: range(A.global.size)) {
  Contact contact {0, 0, {}, {}, -inf /*vertexIndex, invalid, nanf, nanf*/};
  vec3 O = A.global[vertexIndex];
  for(size_t faceIndex: range(B.faces.size)) {
   Face f = B.faces[faceIndex];
   vec3 a = B.global[f.a];
   vec3 b = B.global[f.b];
   vec3 c = B.global[f.c];
   vec3 E1 = b-a, E2 = c-a;
   vec3 D = -cross(E1, E2);
   D /= length(D);
   vec3 P = cross(D, E2);
   float det = dot(P, E1);
   assert_(det > 0.000001, det);
   //if(det < 0.000001) { error(i%2, faceIndex%2); /*return {_0f,_0f,_0f,0,0,0};*/ }
   vec3 T = O - a;
   float invDet = 1 / det;
   float u = invDet * dot(P, T);
   //u = max(0, u);
   //Contact contact {_0f,_0f,_0f,inf,0,0};
   if(u < 0 || u > 1) goto break_;
   vec3 Q = cross(T, E1);
   float v = invDet * dot(Q, D);
   //v = max(0, v);
   if(v < 0 || u + v  > 1) goto break_;
   float t = dot(Q, E2) * invDet;
   //if(t<0) return {_0f,_0f,_0f,0,0,0};
   //if(t < R) flag.append({faceIndex,u,v});
   //contact = {float3(tA::radius)*D, O+t*D, V[0]+/*-*/D, t-tA::radius, u, v};
   if(t > R) goto break_;
   if(t > contact.t) {
    /*contact.face = faceIndex;
    contact.u = u;
    contact.v = v;
    contact.t = t;*/
    contact.P1 = O;
    contact.P2 = O + t * D;
   }
  } /*else*/ { // Inside all faces
   return contact;
  }
  break_:;
 }
 return {0, 0, {}, {}, -inf};
}

// Cylinder - Cylinder
void edgeEdge(const Polyhedra& A, const Polyhedra& B, float R = 0) {
 for(Edge a: A.edges) {
  Contact contact {0, 0, {}, {}, -inf};
  vec3 A1 = A.global[a.a], A2 = A.global[a.b];
  for(Edge b: B.edges) {
   vec3 B1 = A.global[b.a], B2 = A.global[b.b];
   v4sf A, B;
   closest(A1, A2, B1, B2, A, B);
  }
 }
}

Contact contact(const Polyhedra& A, const Polyhedra& B, float R = 0) {
 return vertexFace(A, B, R) ?: vertexFace(B, A, R) ?: edgeEdge(A, B, R);
}

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;

 array<Contact> contacts;

 PolyhedraSimulation() {
  Random random;
  while(polyhedras.size < 2) {
   const float d = 16;
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
    Polyhedra p{
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices),
       copyRef(ref<Face>{{0,1,2}, {0,2,3}, {0,3,1}, {1,3,2}}),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}}),
     buffer<vec3>(4), radius
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
 }
 void step() {
  const float dt = 1./60;
  vec3 center = ::mean(apply(polyhedras, [](const Polyhedra& p)->vec3{ return p.position;}));
  contacts.clear();
  for(auto& p: polyhedras) {
   for(size_t a: range(p.local.size)) {
    p.global[a] = toVec3(p.position + qapply(p.rotation, p.local[a]));
   }
  }
  for(size_t a: range(polyhedras.size)) {
   size_t contactCount = 0;
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
    Contact contact = ::contact(A, B);
    if(contact) {
     contact.a = a, contact.b = b;
     contacts.append(contact);
     contactCount++;
    }
   }
   if(contactCount == 0) { // Moves toward center while no contacts
    A.position += (center - A.position) * dt;
   }
  }
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

  for(Polyhedra& p: polyhedras) {
   for(Edge e: p.edges) {
    vec2 A = offset + scale * toVec3(qapply(viewRotation, p.global[e.a] - center)).xy();
    vec2 B = offset + scale * toVec3(qapply(viewRotation, p.global[e.b] - center)).xy();
    graphics->lines.append(A, B);
   }
   for(Face f: p.faces) {
    vec3 A = p.global[f.a], B = p.global[f.b], C = p.global[f.c];
    vec3 F = (A+B+C)/3.f;
    vec3 N = cross(B-A, C-A);
    assert_(length(N), A,B,C);
    N /= length(N);
    vec2 P1 = offset + scale * toVec3(qapply(viewRotation, F - center)).xy();
    vec2 P2 = offset + scale * toVec3(qapply(viewRotation, F+N - center)).xy();
    graphics->lines.append(P1, P2);
   }
  }

  for(Contact& contact: contacts) {
   const Polyhedra& A = polyhedras[contact.a];
   const Polyhedra& B = polyhedras[contact.b];
   /*Face f = A.faces[contact.face];
   vec3 a = A.global[f.a];
   vec3 b = A.global[f.b];
   vec3 c = A.global[f.c];
   vec3 E1 = b-a, E2 = c-a;
   vec3 D = -cross(E1, E2);
   D /= length(D);

   vec3 O = B.global[contact.vertex] - center;
   vec3 R = O + contact.t * D;
   vec2 P1 = offset + scale * toVec3(qapply(viewRotation, O)).xy();
   vec2 P2 = offset + scale * toVec3(qapply(viewRotation, R)).xy();*/
   //log(P1, P2, length(P2-P1));
   P1 = contact.P1;
   P2 = contact.P2;
   graphics->lines.append(P1, P2);
  }
  return graphics;
 }
};

struct PolyhedraApp : PolyhedraView {
 unique<Window> window = ::window(this);
 PolyhedraApp() {
  step();
  window->presentComplete = [this]{ step(); window->render(); };
 }
} polyhedra;
