#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "png.h"
#include "time.h"
#include "parallel.h"
#include "gl.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
#include "simulation.h"
FILE(shader_glsl)

template<Type tA> vec4f toGlobal(tA& A, size_t a, vec4f localA) {
 return A.position[a] + qapply(A.rotation[a], localA);
}

struct SimulationView : Simulation, Widget, Poll {
 Time renderTime;

 void step() {
  Simulation::step();
  if(encoder) viewYawPitch.x += 2*PI*dt / 16;
  window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 4e9) {
   if(1) {
    log(timeStep*this->dt, totalTime, (timeStep-lastReportStep) / (elapsed*1e-9), grain.count, wire.count);
    log("grain",str(grainTime, stepTime), "wire",str(wireTime, stepTime));
   log("grainInit",str(grainInitializationTime, grainTime),
       "grainLattice",str(grainLatticeTime, grainTime),
       "grainContact",str(grainContactTime, grainTime),
       "grainIntegration",str(grainIntegrationTime, grainTime));
   log("wireLatticeTime",str(wireLatticeTime, wireTime),
       "wireContact",str(wireContactTime, wireTime),
       "wireIntegration",str(wireIntegrationTime, wireTime));
   }
   lastReport = realTime();
   lastReportStep = timeStep;
#if PROFILE
   requestTermination();
#endif
  }
  queue();
 }
 void event() {
#if PROFILE
  static unused bool once =
    ({ extern void profile_reset(); profile_reset(); true; });
#endif
  step();
 }

 unique<Window> window = ::window(this, -1);
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 2./(32*Grain::radius);
 vec3 translation = 0;
 vec3 rotationCenter = 0;
 Thread simulationThread;
 unique<Encoder> encoder = nullptr;
 GLFrameBuffer target {int2(1280,720)};
 bool showPlot = 0 && processState < Done;

 SimulationView() : Poll(0, POLLIN, simulationThread) {
  window->actions[F12] = [this]{
   if(existsFile(str(timeStep*dt)+".png")) log(str(timeStep*dt)+".png exists");
   else writeFile(str(timeStep*dt)+".png",encodePNG(target.readback()), home());
  };
  window->actions[Key('p')] = [this]{ showPlot = !showPlot; };
  window->actions[Return] = [this]{
   if(processState < Done) processState++;
   log(int(processState), "grain", grain.count, "wire", wire.count);
  };
  if(arguments().contains("export")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

  simulationThread.spawn();
  queue();
 }
 ~SimulationView() {
  log("~", "grain", grain.count, "wire", wire.count);
 }
 vec2 sizeHint(vec2) override { return vec2(1050, /*1050*720/1280*/720); }
 shared<Graphics> graphics(vec2) override {
  renderTime.start();

  const float Dt = 1./60;
  {
   vec4f min = _0f, max = _0f;
   //Locker lock(this->lock);
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
   this->rotationCenter = this->rotationCenter*(1-Dt) + Dt*toVec3(rotationCenter);
  }

  vec4f viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                 angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 min = -1./32, max = 1./32;
  {
   for(vec4f p: grain.position.slice(0, grain.count)) {
    assert(p[3] == 0, p);
    vec3 O = toVec3(qapply(viewRotation, p) - rotationCenter);
    min = ::min(min, O - vec3(vec2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(Grain::radius), 0)); // Parallel
   }
   for(size_t i: range(wire.count)) {
    vec3 O = toVec3(qapply(viewRotation, wire.position[i]) - rotationCenter);
    min = ::min(min, O - vec3(vec2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(Grain::radius), 0)); // Parallel
   }
  }

  vec2 size (target.size.x, target.size.y);
  vec2 viewSize = size;
  if(showPlot) {
   viewSize = vec2(720, 720);
   if(plots.size>2) viewSize = vec2(360,720);
  }

  vec3 scaleMax = ::min(max, vec3(vec2(16*Grain::radius), 32*Grain::radius));
  vec3 scaleMin = ::max(min, vec3(vec2(-16*Grain::radius), 0));
  vec3 scale (2*::min(viewSize/(scaleMax-scaleMin).xy())/size,
                    -1/(2*(max-min).z));
  this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
  scale.xy() = this->scale;

  vec3 translation = this->translation = vec3((size-viewSize)/size, 0);

  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-rotationCenter);

  map<rgb3f, array<vec3>> lines;

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  {Locker lock(this->lock);
  if(grain.count) {
   buffer<vec3> positions {grain.count*6};
   for(size_t i: range(grain.count)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * toVec3(grain.position[i]);
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(Grain::radius); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(Grain::radius); // Parallel
    positions[i*6+0] = vec3(min, O.z);
    positions[i*6+1] = vec3(max.x, min.y, O.z);
    positions[i*6+2] = vec3(min.x, max.y, O.z);
    positions[i*6+3] = vec3(min.x, max.y, O.z);
    positions[i*6+4] = vec3(max.x, min.y, O.z);
    positions[i*6+5] = vec3(max, O.z);

    for(int d: range(0)) {
     vec3 axis = 0; axis[d] = 1;
     lines[rgb3f(vec3(axis))].append(viewProjection*toVec3(toGlobal(grain, i, _0f)));
     lines[rgb3f(vec3(axis))].append(viewProjection*toVec3(toGlobal(grain, i,
                                                       float(Grain::radius/2)*axis)));
    }
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   GLBuffer rotationBuffer (apply(grain.rotation.slice(0, grain.count),
                [=](vec4f q) -> vec4f { return conjugate(qmul(viewRotation,q)); }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   shader["radius"] = float(scale.z/2 * Grain::radius);
   vertexArray.draw(Triangles, positions.size);
  }}

  {Locker lock(this->lock);
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
    vec3 n = scale*float(Wire::radius)*vec3(t.y, -t.x, 0);
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
   shader["radius"] = float(scale.z/2 * Wire::radius);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }}

  if(load.count) {
   vec3 min (-vec2(-side.castRadius), load.position[0][2]);
   vec3 max (+vec2(-side.castRadius), load.position[0][2]);
   for(int i: range(0b111 +1)) for(int j: {(i&0b110) |0b001, (i&0b101) |0b010, (i&0b011) |0b100}) {
    if(i<j) {
     auto p = [=](int i) {
      return vec3(i&0b001?max[0]:min[0], i&0b010?max[1]:min[1], i&0b100?max[2]:min[2]);
      };
      lines[rgb3f(1,0,0)].append(viewProjection*p(i));
      lines[rgb3f(1,0,0)].append(viewProjection*p(j));
     }
    }
  }

  if(1) glDepthTest(false);

  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});
  static GLVertexArray vertexArray;
  for(auto entry: lines) {
   shader["uColor"] = entry.key;
   GLBuffer positionBuffer (entry.value);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Lines, entry.value.size);
  }
  lines.clear();

  if(plots) {
   Image target(int2(size.x-viewSize.x, size.y), true);
   target.clear(byte4(byte3(0xFF), 0));
   this->lock.lock();
   auto graphics = plots.graphics(vec2(target.size), Rect(vec2(target.size)));
   this->lock.unlock();
   render(target, graphics);
   GLTexture image = flip(move(target));
   static GLShader shader {::shader_glsl(), {"blit"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   shader["image"] = 0; image.bind(0);
   vec2 min (-1,-1), max (vec2(target.size)/size*float(2)-vec2(1));
   GLBuffer positionBuffer (ref<vec2>{
                  vec2(min.x,min.y), vec2(max.x,min.y), vec2(min.x,max.y),
                  vec2(min.x,max.y), vec2(max.x,min.y), vec2(max.x,max.y)});
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             2, Float, positionBuffer);
   glBlendAlpha();
   vertexArray.draw(Triangles, 6);
  }

  if(encoder) {
   encoder->writeVideoFrame(target.readback());
  }
  int offset = (target.size.x-window->size.x)/2;
  target.blit(0, window->size, int2(offset, 0), int2(target.size.x-offset, target.size.y));
  renderTime.stop();

  array<char> s = str(int(processState), timeStep*this->dt, grain.count, wire.count/*, kT, kR*/
                      /*,staticFrictionCount2, dynamicFrictionCount2*/);
  if(load.count) s.append(" "_+str(load.position[0][2]));
  window->setTitle(s);
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  vec2 delta = cursor-lastPos; lastPos=cursor;
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
   if(encoder) encoder = nullptr;
  }
  else return false;
  return true;
 }
} view;
