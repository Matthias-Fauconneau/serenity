#include "matrix.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "encoder.h"
#include "gl.h"
FILE(shader_glsl)
typedef v4sf vec4f;

struct SnapshotView : Widget {
 String id;
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles

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

 SnapshotView() {}
 SnapshotView(string id) : id(copyRef(id)) {
  if(!id) return;
  assert_(existsFile(id+".grain"), id, currentWorkingDirectory().list(Files));
  assert_(existsFile(id+".side"), id, currentWorkingDirectory().list(Files));
  if(existsFile(id+".grain")) {TextData s(readFile(id+".grain"));
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
  if(existsFile(id+".wire")) {TextData s(readFile(id+".wire"));
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
  if(existsFile(id+".side")) {TextData s(readFile(id+".side"));
   while(s) {
    {float x = s.decimal(); s.skip(' ');
     float y = s.decimal(); s.skip(' ');
     float z = s.decimal(); s.skip(' ');
     side.position.append(v4sf{x,y,z,0});}
    {float x = s.decimal(); s.skip(' ');
     float y = s.decimal(); s.skip(' ');
     float z = s.decimal(); if(!s.match('\n')) break;
     side.position.append(v4sf{x,y,z,0});}
   }
  }
  log(id, grain.count, wire.count, side.position.size);
 }
 void snapshot() {
  writeFile(id+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }

 vec2 sizeHint(vec2 size) override {
  if(!grain.count) return 0;
  if(size.x || size.y) return max(size.x, size.y);
  return vec2(1050/3, 1050/3/**size.y/size.x*/);
 }

 Image render(int2 size) {
  vec4f viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 scale, translation; // Fit view
  array<vec3> grainPositions (grain.count); // Rotated, Z-Sorted
  {
   vec3 min = inf, max = -inf;
   for(vec4f p: grain.position.slice(0, grain.count)) {
    vec3 O = toVec3(qapply(viewRotation, p));
    min = ::min(min, O - vec3(grain.radius));
    max = ::max(max, O + vec3(grain.radius));
    size_t i = 0;
    while(i < grainPositions.size && grainPositions[i].z < O.z) i++;
    grainPositions.insertAt(i, O);
   }
   scale = vec3(vec2(2/::max(max.x-min.x, max.y-min.y)), 2/(max-min).z);
   translation = -vec3((min+max).xy()/2.f, min.z);
  }

  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    //.translate(vec3(0,0,-1))
    .scale(scale)
    .translate(translation);
  mat4 rotatedViewProjection = mat4(viewProjection).rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x);

  if(!target) target = GLFrameBuffer(size);
  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  if(grain.count) { // Grains
   buffer<vec3> positions {grain.count*6};
   for(size_t i: range(grain.count)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * grainPositions[i]; //toVec3(grain.position[i]);
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(grain.radius) - vec2(2.f/size.x); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(grain.radius) + vec2(2.f/size.x); // Parallel
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
   shader["hpxRadius"] = 1 / (size.x * scale.x * grain.radius);
   vertexArray.draw(Triangles, positions.size);
  }

  if(wire.count>1) { // Wire
   size_t wireCount = wire.count;
   buffer<vec3> positions {(wireCount-1)*6};
   size_t s = 0;
   for(size_t i: range(wireCount-1)) {
    vec3 a (toVec3(wire.position[i])), b (toVec3(wire.position[i+1]));
    if(length(a.xy()) > 0.03) continue;
    if(length(b.xy()) > 0.03) continue;
    // FIXME: GPU quad projection
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    vec2 r = B.xy()-A.xy();
    float l = length(r);
    vec2 t = r/l;
    vec3 n = scale*float(wire.radius)*vec3(t.y, -t.x, 0); // FIXME: hpx
    vec3 P[4] {A-n, A+n, B-n, B+n};
    positions[s*6+0] = P[0];
    positions[s*6+1] = P[1];
    positions[s*6+2] = P[2];
    positions[s*6+3] = P[2];
    positions[s*6+4] = P[1];
    positions[s*6+5] = P[3];
    s++;
   }
   positions.size = s*6;
   static GLShader shader {::shader_glsl(), {"cylinder"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["radius"] = float(scale.z/2 * wire.radius);
   shader["hpxRadius"] = 1 / (size.x * scale.x * wire.radius);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }

  if(side.position) { // Side
   static GLShader shader {::shader_glsl(), {"flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);

   // World space bounding box
   vec3 min = inf, max = -inf;
   for(vec4f p: grain.position.slice(0, grain.count)) {
    min = ::min(min, toVec3(p) - vec3(grain.radius));
    max = ::max(max, toVec3(p) + vec3(grain.radius));
   }

   buffer<vec3> positions {side.position.size};
   size_t s = 0;
   for(size_t i: range(side.position.size/2)) {
    vec3 a (toVec3(side.position[i*2+0]));
    vec3 b (toVec3(side.position[i*2+1]));
    if(a.z < min.z || a.z > max.z) continue;
    if(b.z < min.z || b.z > max.z) continue;
    // FIXME: GPU projection
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    positions[s*2+0] = A;
    positions[s*2+1] = B;
    s++;
   }
   positions.size = s*2;
   //shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  return target.readback();
 }

 shared<Graphics> graphics(vec2 widgetSize) override {
  shared<Graphics> graphics;
  graphics->blits.append(0, widgetSize, render(int2(widgetSize)));
  return graphics;
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
};
