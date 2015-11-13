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
 vec3 position;
 v4sf rotation;
 buffer<vec3> vertices; // Vertex positions in local frame
 buffer<vec4> faces; // Face planes in local frame (normal, distance)
 buffer<Edge> edges; // Edge vertex indices
 //buffer<vec3> global; // Vertex positions in global frame
 static constexpr float boundingRadius = 1;
 static constexpr float overlapRadius = 1./16;
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
 size_t vertexIndex = invalid, edgeIndex = invalid, faceIndex = invalid;
 vec3 rbC = 0; // contact point relative to B position (unrotated)
 vec3 raC = 0; // Contact point relative to A position (unrotated)
 vec3 N = 0;
 float depth = inf;
 explicit operator bool() { return depth < inf; }
};

Contact vertexFace(const Polyhedra& A, size_t vertexIndex, const Polyhedra& B) {
 Contact contact;
 vec3 laA = A.vertices[vertexIndex];
 vec3 raA = qapply(A.rotation, laA);
 vec3 gA = A.position + raA;
 vec3 rbA = gA - B.position;
 vec3 lbA = qapply(conjugate(B.rotation), rbA); // Transforms vertex to B local frame
 // Clips against all planes (convex polyhedra)
 for(vec4 plane: B.faces) {
  if(dot(plane.xyz(), lbA) <= plane.w) continue;
  goto break_;
 } /*else*/ { // P inside polyhedra B
  for(size_t faceIndex: range(B.faces.size)) {
   vec4 plane = B.faces[faceIndex];
   // Projects vertex on plane (B frame)
   float t = plane.w - dot(plane.xyz(), lbA);
   if(t <= -A.overlapRadius-B.overlapRadius || t >= A.overlapRadius+B.overlapRadius) continue;
   float depth = t + A.overlapRadius + B.overlapRadius;
   if(depth < contact.depth) {
    contact.vertexIndex = vertexIndex;
    contact.faceIndex = faceIndex;
    contact.N = plane.xyz();
    assert_(depth > 0);
    contact.depth = depth;
    vec3 lbB = lbA + t * plane.xyz();
    vec3 rbB = qapply(B.rotation, lbB);
    vec3 rbC = (rbA + rbB)/2.f;
    contact.rbC = rbC;
    contact.raC = rbC + B.position - A.position;
   }
  }
 }
 break_:;
 return contact;
}

Contact edgeEdge(const Polyhedra& A, const Polyhedra& B) {
 for(Edge eA: A.edges) {
  vec3 A1 = global(A, eA.a), A2 = global(A, eA.b);
  for(Edge eB: B.edges) {
   vec3 B1 = global(B, eB.a), B2 = global(B, eB.b);
   vec3 gA, gB;
   closest(A1, A2, B1, B2, gA, gB);
   vec3 r = gB-gA;
   float L = ::length(r);
   float depth = A.overlapRadius + B.overlapRadius - L;
   if(depth > 0) {
    Contact contact;
    contact.N = r/L;
    contact.depth = depth;
    vec3 gC = (gA+gB)/2.f;
    contact.rbC = gC - B.position;
    contact.raC = gC - A.position;
    return contact;
   }
  }
 }
 return Contact();
}

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
    contact.vertexIndex = vertexIndex;
    contact.edgeIndex = edgeIndex;
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

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;
 buffer<vec3> velocity, angularVelocity;
 const float dt = 1./(60*16);
 const float damping = 1;
 const float density = 1;
 const float E = 1000;
 const float volume = 1; // FIXME
 const float angularMass = 1; // FIXME
 const vec3 g {0,0,-10};
 const float d = 4, D = 5;
 const float frictionCoefficient = 1;//0.1;

 size_t t = 0;
 array<Contact> contacts;

 struct Force { vec3 origin, force; };
 array<Force> forces;

 PolyhedraSimulation() {
  Random random;
  while(polyhedras.size < 8) {
   vec3 position(d*random(), d*random(), 1+d*random());
   const float r = 1./sqrt(3.);
   ref<vec3> vertices{vec3(r,r,r), vec3(r,-r,-r), vec3(-r,r,-r), vec3(-r,-r,r)};
   float boundingRadius = 1;
   for(auto& p: polyhedras) {
    if(length(p.position - position) <= p.boundingRadius + boundingRadius)
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
     position,
     {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(vertices),
       move(faces),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}})
         //,radius
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
  velocity = buffer<vec3>(polyhedras.size);
  velocity.clear(0);
  angularVelocity = buffer<vec3>(polyhedras.size);
  angularVelocity.clear(0);
 }

 bool step() {
  bool status = false;
  buffer<vec3> force (polyhedras.size), torque (polyhedras.size);
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& p = polyhedras[a];
   {
    vec3 f = g * density * volume;
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
     force[a] += f;
     torque[a] += cross(rA, f);
     forces.append(gA, f);
    }
   }
  }

  array<Contact> contacts;
  for(size_t a: range(polyhedras.size)) {
   Polyhedra& A = polyhedras[a];
   for(size_t b: range(polyhedras.size)) {
    if(a==b) continue;
    Polyhedra& B = polyhedras[b];
    for(size_t vertexIndex: range(A.vertices.size)) {
     {// Vertex - Face (Sphere - Plane)
      Contact contact = ::vertexFace(A, vertexIndex, B);
      if(contact) {
       contact.a = a, contact.b = b;
       contacts.append(contact);

       vec3 N = contact.N; float depth = contact.depth;
       vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, velocity[a])) * N;

       force[a] += f;
       torque[a] += cross(contact.raC, f);
       forces.append(A.position + contact.raC, f);

       force[b] += -f;
       torque[b] += cross(contact.rbC, -f);
       forces.append(B.position + contact.rbC, -f);

       if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("SP", a, b, contact.depth, A.overlapRadius+B.overlapRadius);  goto end; }
      }
     }
     if(a < b) {// Edge - Edge (Cylinder - Cylinder) // FIXME: double force evaluation
      Contact contact = ::edgeEdge(A, B);
      if(contact) {
       contact.a = a, contact.b = b;
       contacts.append(contact);

       vec3 N = contact.N; float depth = contact.depth;
       vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, velocity[a])) * N;

       force[a] += f;
       torque[a] += cross(contact.raC, f);
       forces.append(A.position + contact.raC, f);

       force[b] += -f;
       torque[b] += cross(contact.rbC, -f);
       forces.append(B.position + contact.rbC, -f);

       //if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("EE", a, b, contact.depth, A.overlapRadius+B.overlapRadius);  goto end; }
      }
     }
     // Vertex - Edge (Sphere - Cylinder)
     Contact contact = ::vertexFace(A, vertexIndex, B);
     if(contact) {
      contact.a = a, contact.b = b;
      contacts.append(contact);

      vec3 N = contact.N; float depth = contact.depth;
      vec3 f = 4.f/3 * E * sqrt(2.f/3*A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, velocity[a])) * N;

      force[a] += f;
      torque[a] += cross(contact.raC, f);
      forces.append(A.position + contact.raC, f);

      force[b] += -f;
      torque[b] += cross(contact.rbC, -f);
      forces.append(B.position + contact.rbC, -f);

      if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("VE", a, b, contact.depth, A.overlapRadius+B.overlapRadius);  goto end; }
     }
     // Vertex - Vertex
     //TODO
    }
   }
  }

  for(size_t a: range(polyhedras.size)) {
   assert_(isNumber(force[a]));
   velocity[a] += dt * force[a];
   vec3 dx = dt * velocity[a];
   polyhedras[a].position += dx;
   assert_(isNumber(polyhedras[a].position));

   polyhedras[a].rotation += float4(dt/2.f) * qmul(angularVelocity[a], polyhedras[a].rotation);
   angularVelocity[a] += dt / angularMass * torque[a];
   polyhedras[a].rotation *= rsqrt(sq4(polyhedras[a].rotation));

  }
  status = true;
  end:
  this->contacts = ::move(contacts);
  this->forces = ::move(forces);
  t++;
  return status;
 }
};


struct PolyhedraView : PolyhedraSimulation, Widget {
 struct State {
  buffer<vec3> position;
  buffer<v4sf> rotation;
  buffer<Contact> contacts;
  buffer<Force> forces;
 };
 array<State> states;

 void record() {
  State state;
  state.position = buffer<vec3>(polyhedras.size);
  state.rotation = buffer<v4sf>(polyhedras.size);
  for(size_t p: range(polyhedras.size)) {
   state.position[p] = polyhedras[p].position;
   state.rotation[p] = polyhedras[p].rotation;
  }
  state.contacts = ::move(contacts);
  state.forces = ::move(forces);
  states.append(move(state));
 }

 size_t viewT;

 vec2 viewYawPitch = vec2(0, PI/3); // Current view angles

 struct {
  vec2 cursor;
  vec2 viewYawPitch;
  size_t viewT;
 } dragStart;


 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  if(event == Press) dragStart = {cursor, viewYawPitch, viewT};
  if(event==Motion && button==LeftButton) {
   viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
   viewYawPitch.y = clamp<float>(0, viewYawPitch.y, PI);
  }
  else if(event==Motion && button==RightButton) {
   if(states) viewT = clamp<int>(0, int(dragStart.viewT) + (states.size-1) * (cursor.x - dragStart.cursor.x) / (size.x/2-1), states.size-1); // Relative
  }
  else return false;
  return true;
 }

 vec2 sizeHint(vec2) override { return 1024; }
 shared<Graphics> graphics(vec2 size) override {
  shared<Graphics> graphics;

  v4sf viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)), angleVector(viewYawPitch.x, vec3(0,0,1)));

  assert_(viewT < states.size, viewT, states.size);
  const State& state = states[viewT];

  // Transforms vertices and evaluates scene bounds
  vec3 min = inf, max = -inf;
  for(size_t p: range(polyhedras.size)) {
   for(size_t vertexIndex: range(polyhedras[p].vertices.size)) {
    vec3 A = qapply(viewRotation, state.position[p] + qapply(state.rotation[p], polyhedras[p].vertices[vertexIndex]));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }

  vec2 scale = size/(max-min).xy();
  scale.x = scale.y = ::min(scale.x, scale.y);
  vec2 offset = - scale * min.xy();

  if(0) for(size_t i: range(4)) {
   vec2 P1 = offset + scale * qapply(viewRotation, vec3(i%2, i<2, 0)*d).xy();
   vec2 P2 = offset + scale * qapply(viewRotation, vec3((i+1)%2, ((i+1)%4)<2, 0)*d).xy();
    assert_(isNumber(P1) && isNumber(P2));
    graphics->lines.append(P1, P2);
 }


  for(size_t p: range(polyhedras.size)) {
   vec3 color = black;
   for(Contact& contact: state.contacts) if(p == contact.a || p == contact.b) color = blue;
   const Polyhedra& A = polyhedras[p];
   vec3 position = states[viewT].position[p];
   v4sf rotation = states[viewT].rotation[p];
   for(Edge eA: A.edges) {
    vec3 A1 = position + qapply(rotation, A.vertices[eA.a]);
    vec3 A2 = position + qapply(rotation, A.vertices[eA.b]);
    vec2 P1 = offset + scale * qapply(viewRotation, A1).xy();
    vec2 P2 = offset + scale * qapply(viewRotation, A2).xy();
    assert_(isNumber(P1) && isNumber(P2), P1, P2);

    vec2 r = P2-P1;
    float l = length(r);
    if(l) {
     vec2 t = r/l;
     vec2 n = scale*A.overlapRadius*vec2(t.y, -t.x);
     graphics->lines.append(P1-n, P2-n, color);
     graphics->lines.append(P1+n, P2+n, color);
    }
   }
  }
  float maxF = 0; for(const Force& force: state.forces) maxF = ::max(maxF, length(force.force));
  for(const Force& force: state.forces) {
   constexpr float maxForcePx = 256;
   vec2 A = offset + scale * qapply(viewRotation, force.origin).xy();
   vec2 B = offset + scale * qapply(viewRotation, force.origin + force.force/maxF*maxForcePx).xy();
   graphics->lines.append(A, B, red);
  }
  return graphics;
 }
};

struct PolyhedraApp : PolyhedraView {
 unique<Window> window = ::window(this);
 PolyhedraApp()  {
  record(); viewT=states.size-1;
  window->presentComplete = [this]{
    for(int unused t: range(16)) if(!step()) { window->presentComplete = {};  break; }
    record(); viewT=states.size-1;
    window->render();
  };
 }
} polyhedra;
