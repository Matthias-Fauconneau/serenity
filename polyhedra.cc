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
 size_t vertexIndexA = invalid, edgeIndexA = invalid, edgeIndexB = invalid, faceIndexB = invalid;
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
  if(dot(plane.xyz(), lbA) <= plane.w + A.overlapRadius + B.overlapRadius) continue;
  goto break_;
 } /*else*/ { // P inside polyhedra B
  for(size_t faceIndex: range(B.faces.size)) {
   // Tighter clips against all other planes (convex polyhedra)
   for(size_t planeIndex: range(B.faces.size)) {
    if(planeIndex == faceIndex) continue;
    vec4 plane = B.faces[planeIndex];
    if(dot(plane.xyz(), lbA) <= plane.w) continue;
    goto continue2_;
   } /*else*/ {
    vec4 bPlane = B.faces[faceIndex];
    // Projects vertex on plane (B frame)
    float t = bPlane.w - dot(bPlane.xyz(), lbA);
    if(t <= -A.overlapRadius-B.overlapRadius || t >= A.overlapRadius+B.overlapRadius) continue;
    float depth = t + A.overlapRadius + B.overlapRadius;
    if(depth < contact.depth) {
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
    }
   }
   continue2_:;
  }
 }
 break_:;
 return contact;
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

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;
 buffer<vec3> velocity, angularVelocity;
 const float dt = 1./(60*16);
 const float damping = 0.1;
 const float density = 1;
 const float E = 10000;
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
   vec3 position(d*random(), d*random(), d*random());
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
    };
    polyhedras.append(move(p));
   }
   break_:;
  }
  float minZ = inf; for(const Polyhedra& p: polyhedras) for(vec3 v: p.vertices) minZ = ::min(minZ, p.position.z + qapply(p.rotation, v).z);
  for( Polyhedra& p: polyhedras) p.position -= minZ;
  velocity = buffer<vec3>(polyhedras.size);
  velocity.clear(0);
  angularVelocity = buffer<vec3>(polyhedras.size);
  angularVelocity.clear(0);
 }

 bool step() {
  buffer<vec3> force (polyhedras.size), torque (polyhedras.size);
  array<Force> forces;

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
     assert_(isNumber(f));
     force[a] += f;
     torque[a] += cross(rA, f);
     //forces.append(gA, f);
    }
   }
  }

  bool status = false;
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
       vec3 rV = velocity[a] + cross(angularVelocity[a], contact.raC) - (velocity[b] + cross(angularVelocity[b], contact.rbC));
       vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N;
       assert_(isNumber(f));

       force[a] += f;
       torque[a] += cross(contact.raC, f);
       forces.append(A.position + contact.raC, f);

       force[b] += -f;
       torque[b] += cross(contact.rbC, -f);
       forces.append(B.position + contact.rbC, -f);

       //if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("SP", a, b, contact.depth, A.overlapRadius+B.overlapRadius); goto end; }
      } else {
       // Vertex - Edge (Sphere - Cylinder) (FIXME: double contact with Edge - Edge ?)
       Contact contact = ::vertexFace(A, vertexIndex, B);
       if(contact) {
        contact.a = a, contact.b = b;
        contacts.append(contact);

        vec3 N = contact.N; float depth = contact.depth;
        vec3 rV = velocity[a] + cross(angularVelocity[a], contact.raC) - (velocity[b] + cross(angularVelocity[b], contact.rbC));
        vec3 f = 4.f/3 * E * sqrt(2.f/3*A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N;
        assert_(isNumber(f));

        force[a] += f;
        torque[a] += cross(contact.raC, f);
        forces.append(A.position + contact.raC, f);

        force[b] += -f;
        torque[b] += cross(contact.rbC, -f);
        forces.append(B.position + contact.rbC, -f);

        if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("VE", a, b, contact.depth, A.overlapRadius+B.overlapRadius);  goto end; }
       }
       // else TODO: Vertex - Vertex
      }
     }
     if(a < b) { // Edge - Edge (Cylinder - Cylinder)
      for(size_t eiA: range(A.edges.size)) {
       Edge eA = A.edges[eiA];
       for(size_t eiB: range(B.edges.size)) {
        Edge eB = B.edges[eiB];
        Contact contact = ::edgeEdge(A, eiA, B, eiB);
        if(contact) {
         contact.a = a, contact.b = b;
         contacts.append(contact);

         vec3 N = contact.N; float depth = contact.depth;
         vec3 rV = velocity[a] + cross(angularVelocity[a], contact.raC) - (velocity[b] + cross(angularVelocity[b], contact.rbC));
         vec3 f = 4.f/3 * E * sqrt(A.overlapRadius) * sqrt(depth) * (depth - damping * dot(N, rV)) * N;
         assert_(isNumber(f), "EE", depth);

         force[a] += f;
         torque[a] += cross(contact.raC, f);
         forces.append(A.position + contact.raC, f);

         force[b] += -f;
         torque[b] += cross(contact.rbC, -f);
         forces.append(B.position + contact.rbC, -f);

         //if(contact.depth > (A.overlapRadius+B.overlapRadius)/2) { log("EE", a, b, contact.depth, A.overlapRadius+B.overlapRadius);  goto end; }
        }
       }
      }
     }
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
  {
   float maxL = 0; for(const Polyhedra& p: polyhedras) for(vec3 v: p.vertices) maxL = ::max(maxL, length(p.position + qapply(p.rotation, v)));
   if(maxL > 16) goto end;
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
 bool running = true;

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
   viewT = clamp<int>(0, int(dragStart.viewT) + (states.size-1) * (cursor.x - dragStart.cursor.x) / (size.x/2-1), states.size-1); // Relative
   running = false;
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
  vec3 min = -D, max = D;
  for(size_t p: range(polyhedras.size)) {
   for(size_t vertexIndex: range(polyhedras[p].vertices.size)) {
    vec3 A = qapply(viewRotation, state.position[p] + qapply(state.rotation[p], polyhedras[p].vertices[vertexIndex]));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }

  vec2 scale = size/(max-min).xy();
  scale.x = scale.y = ::min(scale.x, scale.y);
  vec2 offset = (size - scale * (min.xy()+max.xy())) / 2.f;

  if(0) for(size_t i: range(4)) {
   vec2 P1 = offset + scale * qapply(viewRotation, vec3(i%2, i<2, 0)*d).xy();
   vec2 P2 = offset + scale * qapply(viewRotation, vec3((i+1)%2, ((i+1)%4)<2, 0)*d).xy();
    assert_(isNumber(P1) && isNumber(P2));
    graphics->lines.append(P1, P2);
 }


  for(size_t p: range(polyhedras.size)) {
   const Polyhedra& A = polyhedras[p];
   vec3 position = states[viewT].position[p];
   v4sf rotation = states[viewT].rotation[p];
   for(size_t edgeIndex: range(A.edges.size)) {
    Edge eA = A.edges[edgeIndex];
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
     vec3 color = black;
     for(Contact& contact: state.contacts) {
      if(p == contact.a && edgeIndex == contact.edgeIndexA) color = red;
      if(p == contact.b && edgeIndex == contact.edgeIndexB) color = green;
     }
     graphics->lines.append(P1-n, P2-n, color);
     graphics->lines.append(P1+n, P2+n, color);
    }
   }
   for(size_t vertexIndex: range(A.vertices.size)) {
    vec3 V = position + qapply(rotation, A.vertices[vertexIndex]);
    vec2 P = offset + scale * qapply(viewRotation, V).xy();

    vec3 color = black;
    for(Contact& contact: state.contacts) {
     if(p == contact.a && vertexIndex == contact.vertexIndexA) color = /*TODO: VV*/ contact.edgeIndexB!=invalid ? green /*VE*/ : blue /*VF*/;
    }

    const size_t N = 16;
    for(size_t i: range(N)) {
     float a = i*2*PI/N, b = (i+1)*2*PI/N;
     graphics->lines.append(P+scale*A.overlapRadius*vec2(cos(a),sin(a)), P+scale*A.overlapRadius*vec2(cos(b),sin(b)), color);
    }
   }
   for(size_t faceIndex: range(A.faces.size)) {
    vec4 plane = A.faces[faceIndex];
    vec3 N = qapply(rotation, plane.xyz());
    vec3 V = position + plane.w * N;
    vec2 P = offset + scale * qapply(viewRotation, V).xy();
    vec2 n = qapply(viewRotation, N).xy();

    vec3 color = black;
    for(Contact& contact: state.contacts) {
     if(p == contact.b && faceIndex == contact.faceIndexB) color = red; // TODO: EF, FF
    }

    graphics->lines.append(P, P+16.f*n, color);
   }
  }
  float maxF = 0; for(const Force& force: state.forces) maxF = ::max(maxF, length(force.force));
  for(const Force& force: state.forces) {
   vec2 A = offset + scale * qapply(viewRotation, force.origin).xy();
   vec2 B = offset + scale * qapply(viewRotation, force.origin + force.force/maxF).xy();
   graphics->lines.append(A, B, blue);
  }
  return graphics;
 }
};

struct PolyhedraApp : PolyhedraView {
 unique<Window> window = ::window(this);
 PolyhedraApp()  {
  record(); viewT=states.size-1;
  window->presentComplete = [this]{
   if(!running) return;
   for(int unused t: range(1)) if(!step()) { running = false;  break; }
   record(); viewT=states.size-1;
   window->render();
  };
  window->actions[Space] = [this] { running = true; window->render(); };
 }
} polyhedra;
