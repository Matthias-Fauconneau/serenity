#include "matrix.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
#include "gl.h"
FILE(shader_glsl)
typedef v4sf vec4f;

struct SnapshotView : Widget {
 String id;
 int2 size {1050};
 unique<Window> window = nullptr;
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 2./(32*2.47*1e-3);
 vec3 translation = 0;
 v4sf rotationCenter = _0f;
 GLFrameBuffer target;

 struct Grain {
  float radius;
  size_t count = 0;
  array<vec4f> position;
  struct { vec4f operator[](size_t) const { return _0001f; }} rotation;
 } grain;

 struct Wire {
  float radius;
  size_t count = 0;
  array<vec4f> position;
 } wire;

 struct Side {
  array<vec4f> position;
 } side;

 SnapshotView(string id) : id(copyRef(id)) {
  //window->actions[F12] = {this, &SnapshotView::snapshot};
  {TextData s(readFile(id+".grain"));
   grain.radius = s.decimal(); s.line();
   s.line(); // Header
   while(s) {
    float x = s.decimal(); s.skip(' ');
    float y = s.decimal(); s.skip(' ');
    float z = s.decimal(); s.skip(' ');
    grain.position.append(v4sf{x,y,z,0});
    grain.count++;
    s.line();
   }
  }
  {TextData s(readFile(id+".wire"));
   wire.radius = s.decimal(); s.line();
   s.line(); // Header
   while(s) {
    float x = s.decimal(); s.skip(' ');
    float y = s.decimal(); s.skip(' ');
    float z = s.decimal(); s.skip(' ');
    wire.position.append(v4sf{x,y,z,0});
    wire.count++;
    s.line();
   }
  }
  {TextData s(readFile(id+".side"));
   while(s) {
    {float x = s.decimal(); s.skip(' ');
    float y = s.decimal(); s.skip(' ');
    float z = s.decimal(); s.skip(' ');
    side.position.append(v4sf{x,y,z,0});}
    {float x = s.decimal(); s.skip(' ');
    float y = s.decimal(); s.skip(' ');
    float z = s.decimal(); s.skip('\n');
    side.position.append(v4sf{x,y,z,0});}
   }
  }
  window = ::window(this, -1, mainThread, true);
 }
 void snapshot() {
  writeFile(id+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }

 vec2 sizeHint(vec2) override { return vec2(1050, 1050*size.y/size.x); }
 shared<Graphics> graphics(vec2) override {
  const float Dt = 1./60/2;
  {
   vec4f min = _0f, max = _0f;
   for(size_t i: range(grain.count)) { // FIXME: proper BS
    min = ::min(min, grain.position[i]);
    max = ::max(max, grain.position[i]);
   }
   for(size_t i: range(wire.count)) { // FIXME: proper BS
    min = ::min(min, wire.position[i]);
    max = ::max(max, wire.position[i]);
   }
   vec4f rotationCenter = (min+max)/float4(2);
   rotationCenter[0] = rotationCenter[1] = 0;
   this->rotationCenter = this->rotationCenter*float4(1-Dt) + float4(Dt)*rotationCenter;
  }

  vec4f viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                 angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 min = -1./32, max = 1./32;
  {
   for(vec4f p: grain.position.slice(0, grain.count)) {
    p[3] = 0; // FIXME
    assert(p[3] == 0, p);
    vec3 O = toVec3(qapply(viewRotation, p) - rotationCenter);
    min = ::min(min, O - vec3(vec2(grain.radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(grain.radius), 0)); // Parallel
   }
   for(size_t i: range(wire.count)) {
    vec3 O = toVec3(qapply(viewRotation, wire.position[i]) - rotationCenter);
    min = ::min(min, O - vec3(vec2(grain.radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(grain.radius), 0)); // Parallel
   }
  }

  if(!target) target = GLFrameBuffer(this->size);
  vec2 size (target.size.x, target.size.y);
  vec2 viewSize = size;

  vec3 scaleMax = max; ::min(max, vec3(vec2(16*grain.radius), 32*grain.radius));
  vec3 scaleMin = min; ::max(min, vec3(vec2(-16*grain.radius), 0));
  vec3 scale (2*::min(viewSize/(scaleMax-scaleMin).xy())/size,
                    -1/(2*(max-min).z));
  this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
  scale.xy() = this->scale;

  vec3 translation = this->translation = vec3((size-viewSize)/size, 0);

  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-toVec3(rotationCenter));

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  {
  if(grain.count) {
   buffer<vec3> positions {grain.count*6};
   for(size_t i: range(grain.count)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * toVec3(grain.position[i]);
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(grain.radius); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(grain.radius); // Parallel
    positions[i*6+0] = vec3(min, O.z);
    positions[i*6+1] = vec3(max.x, min.y, O.z);
    positions[i*6+2] = vec3(min.x, max.y, O.z);
    positions[i*6+3] = vec3(min.x, max.y, O.z);
    positions[i*6+4] = vec3(max.x, min.y, O.z);
    positions[i*6+5] = vec3(max, O.z);
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   GLBuffer rotationBuffer (apply(grain.count,
                [=](size_t) -> vec4f { return _0001f/*conjugate(qmul(viewRotation,_0001f))*/; }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   shader["radius"] = float(scale.z/2 * grain.radius*3/4); // reduce Z radius to see membrane mesh on/in grain
   vertexArray.draw(Triangles, positions.size);
  }}

  {
  if(wire.count>1) {
   size_t wireCount = wire.count;
   buffer<vec3> positions {(wireCount-1)*6};
   for(size_t i: range(wireCount-1)) {
    vec3 a (toVec3(wire.position[i])), b (toVec3(wire.position[i+1]));
    // FIXME: GPU quad projection
    vec3 A = viewProjection * a, B= viewProjection * b;
    vec2 r = B.xy()-A.xy();
    float l = length(r);
    vec2 t = r/l;
    vec3 n = scale*float(wire.radius)*vec3(t.y, -t.x, 0);
    vec3 P[4] {A-n, A+n, B-n, B+n};
    positions[i*6+0] = P[0];
    positions[i*6+1] = P[1];
    positions[i*6+2] = P[2];
    positions[i*6+3] = P[2];
    positions[i*6+4] = P[1];
    positions[i*6+5] = P[3];
   }
   static GLShader shader {::shader_glsl(), {"cylinder"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["radius"] = float(scale.z/2 * wire.radius);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }}

  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});
  shader["transform"] = mat4(1);

  {
   buffer<vec3> positions {side.position.size};
   for(size_t i: range(side.position.size/2)) {
    vec3 a (toVec3(side.position[i*2+0]));
    vec3 b (toVec3(side.position[i*2+1]));
    // FIXME: GPU projection
    vec3 A = viewProjection * a, B= viewProjection * b;
    positions[i*2+0] = A;
    positions[i*2+1] = B;
   }
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  int offset = (target.size.x-window->size.x)/2;
  target.blit(0, window->size, int2(offset, 0), int2(target.size.x-offset, target.size.y));
  return shared<Graphics>();
 }

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
} app {arguments()[0]};
