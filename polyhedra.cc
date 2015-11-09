#include "window.h"
#include "matrix.h"

struct Face {
 size_t a, b, c;
};
struct Edge {
 size_t a, b;
};

struct Polyhedra {
 vec3 position;
 v4sf rotation;
 array<vec3> vertices;
 array<Face> faces;
 array<Edge> edges;
};

struct PolyhedraSimulation {
 array<Polyhedra> polyhedras;

 PolyhedraSimulation() {
  Random random;
  while(polyhedras.size < 2) {
   float t0 = 2*PI*random();
   float t1 = acos(1-2*random());
   float t2 = (PI*random()+acos(random()))/2;
   polyhedras.append(
      Polyhedra{
       vec3(random(), random(), random()),
       {sin(t0)*sin(t1)*sin(t2),  cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2), cos(t2)},
       copyRef(ref<vec3>{vec3(1,1,1), vec3(1,-1,-1), vec3(-1,1,-1), vec3(-1,-1,1)}),
       copyRef(ref<Face>{{0,1,2}, {0,1,3}, {0,2,3}, {1,2,3}}),
       copyRef(ref<Edge>{{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}})
      }
      );
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

 vec2 sizeHint(vec2) override { return 512; }
 shared<Graphics> graphics(vec2 size) override {
  shared<Graphics> graphics;

  v4sf viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 min = -3/*inf*/, max = 3/*-inf*/;
  for(auto& p: polyhedras) {
   for(vec3 P: p.vertices) {
    vec3 A = toVec3(qapply(viewRotation, p.position + qapply(p.rotation, P)));
    min = ::min(min, A);
    max = ::max(max, A);
   }
  }

  vec2 scale = size/(max-min).xy();
  scale.x = scale.y = ::min(scale.x, scale.y);
  vec2 offset = - scale * min.xy();

  for(Polyhedra& p: polyhedras) {
   for(Edge e: p.edges) {
    vec2 A = offset + scale * toVec3(qapply(viewRotation, p.position + qapply(p.rotation, p.vertices[e.a]))).xy();
    vec2 B = offset + scale * toVec3(qapply(viewRotation, p.position + qapply(p.rotation, p.vertices[e.b]))).xy();
    graphics->lines.append(A, B);
   }
  }
  return graphics;
 }
} polyhedra;

unique<Window> w = ::window(&polyhedra);
