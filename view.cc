#include "polyhedra.h"

struct PolyhedraView : PolyhedraSimulation, Widget {
 using PolyhedraSimulation::PolyhedraSimulation;

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
  /*vec3 min = -D, max = D;
  for(size_t p: range(polyhedras.size)) {
   for(size_t vertexIndex: range(polyhedras[p].vertices.size)) {
    vec3 A = qapply(viewRotation, state.position[p] + qapply(state.rotation[p], polyhedras[p].vertices[vertexIndex]));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }*/
  float maxL = 0;
  for(size_t p: range(polyhedras.size)) {
   for(size_t vertexIndex: range(polyhedras[p].vertices.size)) {
    maxL = ::max(maxL, length(qapply(viewRotation, state.position[p] + qapply(state.rotation[p], polyhedras[p].vertices[vertexIndex]))));
   }
  }
  vec3 min = -maxL, max = +maxL;

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

Random randomState{2068481770, 118513808};
struct PolyhedraApp : PolyhedraView {
 unique<Window> window = ::window(this);
 PolyhedraApp() : PolyhedraView(randomState) {
  record(); viewT=states.size-1;
  window->presentComplete = [this]{
   if(!running) return;
   for(int unused t: range(1)) if(!step()) { running = false;  break; }
   record(); viewT=states.size-1;
   window->render();
  };
  window->actions[Space] = [this] { running = true; window->render(); };
 }
} app;
