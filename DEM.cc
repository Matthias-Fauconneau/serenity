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

template<Type tA> vec3f toGlobal(tA& A, size_t a, vec3f localA) {
 return A.position[a] + A.rotation[a] * localA;
}

struct SimulationView : Simulation, Widget, Poll {
 Time renderTime;

 bool stop = false;
 void error(string message) {
  stop = true;
  log(message);
  window->setTitle(message);
 }

 void step() {
  Simulation::step();
  if(!rollTest) viewYawPitch.x += 2*PI*dt / 16;
  window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 3e9) {
   /*log(//(timeStep-lastReportStep) / (elapsed*1e-9),
       "++step",str(stepTime, totalTime));*/
   if(1) { log(
       //"+misc",str(miscTime, totalTime),
       "grain",str(grainTime, stepTime),
       "wire",str(wireTime, stepTime)
       /*,"solve",str(solveTime, totalTime)*/);
   log(
       "grainInit",str(grainInitializationTime, grainTime),
       "grainContact",str(grainContactTime, grainTime),
       "grainIntegration",str(grainIntegrationTime, grainTime));
   /*log(
       "wireContact",str(wireContactTime, totalTime),
       "wireBound",str(wireBoundTime, totalTime),
       "wireTensionTime",str(wireTensionTime, totalTime),
       "wireFriction",str(wireFrictionTime, totalTime),
       "wireIntegration",str(wireIntegrationTime, totalTime));*/
   }
   lastReport = realTime();
   lastReportStep = timeStep;
#if PROFILE
   requestTermination();
#endif
  }
  if(!stop) queue();
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
 vec2f lastPos; // for relative cursor movements
 vec2f viewYawPitch = vec2f(0, rollTest?0:-PI/3); // Current view angles
 vec2f scale = 2./(32*Grain::radius);
 vec3f translation = 0;
 vec3f rotationCenter = 0;
#if THREAD
 Thread simulationThread;
#endif
 unique<Encoder> encoder = nullptr;
 GLFrameBuffer target {int2(1280,720)};
 bool showPlot = 0 && processState < Done;

 SimulationView()
#if THREAD
  : Poll(0, POLLIN, simulationThread)
#endif
 {
  window->actions[F12] = [this]{ writeFile(str(timeStep*dt)+".png",
                                          encodePNG(target.readback()), home()); };
  window->actions[RightArrow] = [this]{ if(stop) queue(); };
  window->actions[Space] = [this]{ stop=!stop; if(!stop) queue(); };
  window->actions[Key('p')] = [this]{ showPlot = !showPlot; };
  window->actions[Return] = [this]{
   if(processState < Done) processState++;
   log("grain", grain.count, "wire", wire.count);
  };
  /*window->actions[Key('E')] = [this] {
   if(!encoder) {
    encoder = unique<Encoder>("tas.mp4");
    encoder->setH264(window->Window::size, 60);
    encoder->open();
   }
  };*/
  if(arguments().contains("export")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

#if THREAD
  simulationThread.spawn();
#endif
  queue();
 }
 ~SimulationView() {
  log("~", "grain", grain.count, "wire", wire.count);
 }
 vec2f sizeHint(vec2f) override { return vec2f(1050, 1050*720/1280); }
 shared<Graphics> graphics(vec2f) override {
  renderTime.start();

  const real Dt = 1./60;
  {
   vec3f min = 0, max = 0;
   //Locker lock(this->lock);
   for(size_t i: range(grain.count)) { // FIXME: proper BS
    min = ::min(min, vec3f(grain.position[i]));
    max = ::max(max, vec3f(grain.position[i]));
   }
   for(size_t i: range(wire.count)) { // FIXME: proper BS
    min = ::min(min, vec3f(wire.position[i]));
    max = ::max(max, vec3f(wire.position[i]));
   }
   vec3f rotationCenter = (min+max)/float(2);
   rotationCenter.xy() = 0;
   if(!rollTest)
    this->rotationCenter = this->rotationCenter*float(1-Dt)
                                       + float(Dt)*rotationCenter;
    //this->rotationCenter = rotationCenter;
  }

  quat viewRotation = angleVector(viewYawPitch.y, vec3f(1,0,0)) *
                                 angleVector(viewYawPitch.x, vec3f(0,0,1));

  vec3f min = -1./32, max = 1./32;
  {//Locker lock(this->lock);
   for(size_t i: range(grain.count)) {
    vec3f O = viewRotation * vec3f(grain.position[i])-rotationCenter;
    min = ::min(min, O - vec3f(vec2f(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3f(vec2f(Grain::radius), 0)); // Parallel
   }
   for(size_t i: range(wire.count)) {
    vec3f O = viewRotation * vec3f(wire.position[i])-rotationCenter;
    min = ::min(min, O - vec3f(vec2f(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3f(vec2f(Grain::radius), 0)); // Parallel
   }
  }

  vec2f size (target.size.x, target.size.y);
  vec2f viewSize = size;
  if(showPlot) {
   viewSize = vec2f(720, 720);
   if(plots.size>2) viewSize = vec2f(360,720);
  }

  vec3f scale (2*::min(viewSize/(max-min).xy())/size,
                    -1/(2*(max-min).z));
  if(!rollTest) this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
  scale.xy() = this->scale;

  //if(rollTest) scale.xy() = vec2f(viewSize.x/(16*Grain::radius))/viewSize;
  vec3f fitTranslation = -scale*(min+max)/float(2);
  //vec2 aspectRatio (size.x/size.y, 1);
  vec3f translation = this->translation = vec3f((size-viewSize)/size, 0);
  //this->translation*(1-Dt) + Dt*fitTranslation;
  /*mat4 viewProjection;
  for(int e: range(3)) {
   vec3 axis = 0; axis[e] = 1;
   viewProjection[e] = vec4(scale*(viewRotation*axis),
                            (translation + scale*(viewRotation*(-rotationCenter)))[e]);
  }*/
  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-rotationCenter);

  map<rgb3f, array<vec3>> lines;
  //lines.clear();

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);


  {Locker lock(this->lock);
  if(grain.count) {
   buffer<float3> positions {grain.count*6};
   for(size_t i: range(grain.count)) {
    // FIXME: GPU quad projection
    vec3f O = viewProjection * vec3f(grain.position[i]);
    vec2f min = O.xy() - vec2f(scale.xy()) * vec2f(Grain::radius); // Parallel
    vec2f max = O.xy() + vec2f(scale.xy()) * vec2f(Grain::radius); // Parallel
    positions[i*6+0] = float3(min, O.z);
    positions[i*6+1] = float3(max.x, min.y, O.z);
    positions[i*6+2] = float3(min.x, max.y, O.z);
    positions[i*6+3] = float3(min.x, max.y, O.z);
    positions[i*6+4] = float3(max.x, min.y, O.z);
    positions[i*6+5] = float3(max, O.z);

    for(int d: range(0)) {
     vec3f axis = 0; axis[d] = 1;
     lines[rgb3f(vec3f(axis))].append(viewProjection*vec3f(toGlobal(grain, i, 0)));
     lines[rgb3f(vec3f(axis))].append(viewProjection*vec3f(toGlobal(grain, i,
                                                       float(Grain::radius/2)*axis)));
    }
#if DBG_FRICTION && 0
    for(const Friction& f : grain.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3 A = toGlobal(grain, i, f.localA);
     size_t b = f.index;
     vec3 B =
       b < grain.base+grain.count ? toGlobal(grain, b-grain.base, f.localB) :
       b < wire.base+wire.count ? toGlobal(wire, b-wire.base, f.localB) :
       b == floor.base ? toGlobal(floor, b-floor.base, f.localB) :
       b==side.base ? toGlobal(side, b-side.base, f.localB) :
       (::error("grain", b), vec3());
     vec3f vA = viewProjection*vec3f(A), vB=viewProjection*vec3f(B);
     if(length(vA-vB) < 16/size.y) vA.y -= 8/size.y, vB.y += 8/size.y;
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   assert_(grain.count);
   GLBuffer rotationBuffer (apply(grain.rotation.slice(0, grain.count),
                [=](quat q) -> quat { return (viewRotation*q).conjugate(); }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   /*GLBuffer colorBuffer (grain.color.slice(start));
   shader.bind("colorBuffer"_, colorBuffer, 1);*/
   shader["radius"] = float(scale.z/2 * Grain::radius);
   //glBlendAlpha();
   vertexArray.draw(Triangles, positions.size);
  }}

  {Locker lock(this->lock);
  if(wire.count>1) {
   size_t wireCount = wire.count;
   buffer<float3> positions {(wireCount-1)*6};
   for(size_t i: range(wireCount-1)) {
    float3 a (wire.position[i]), b (wire.position[i+1]);
    // FIXME: GPU quad projection
    float3 A = viewProjection * a, B= viewProjection * b;
    float2 r = B.xy()-A.xy();
    float l = length(r);
    float2 t = r/l;
    float3 n = float3(scale)*float(Wire::radius)*float3(t.y, -t.x, 0);
    float3 P[4] {A-n, A+n, B-n, B+n};
    positions[i*6+0] = P[0];
    positions[i*6+1] = P[1];
    positions[i*6+2] = P[2];
    positions[i*6+3] = P[2];
    positions[i*6+4] = P[1];
    positions[i*6+5] = P[3];

#if DBG_FRICTION
    for(const Friction& f : wire.frictions[i]) {
     if(f.lastUpdate < timeStep-1) continue;
     vec3f A = toGlobal(wire, i, f.localA);
     size_t b = f.index;
     if(b >= grain.base+grain.count) continue;
     vec3f B =
      b < grain.base+grain.count ? toGlobal(grain, b-grain.base, f.localB) :
      b < wire.base+wire.count ? /*toGlobal(wire, b-wire.base, f.localB)*/
                               (::error(wire.base,b,wire.count), vec3f()) :
      b==floor.base ? toGlobal(floor, b-floor.base, f.localB) :
      b==side.base ? toGlobal(side, b-side.base, f.localB) :
      (::error("wire", b), vec3f());
     vec3f vA = viewProjection*vec3f(A), vB=viewProjection*vec3f(B);
     if(length(vA-vB) < 2/size.y) {
      if(vA.y<vB.y) vA.y -= 4/size.y, vB.y += 4/size.y;
      else vA.y += 4/size.y, vB.y -= 4/size.y;
     }
     //log(f.color, (vA+vec3f(1.f))/2.f*float(size.x), (vB+vec3f(1.f))/2.f*float(size.y));
     lines[f.color].append(vA);
     lines[f.color].append(vB);
    }
#endif
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
   //Locker lock(this->lock);
   vec3 min (-vec2(-side.castRadius), load.position[0].z);
   vec3 max (+vec2(-side.castRadius), load.position[0].z);
   for(int i: range(0b111 +1)) for(int j: {(i&0b110) |0b001, (i&0b101) |0b010, (i&0b011) |0b100}) {
    if(i<j) {
     auto p = [=](int i) {
      return vec3f(i&0b001?max[0]:min[0], i&0b010?max[1]:min[1], i&0b100?max[2]:min[2]);
      };
      lines[rgb3f(1,0,0)].append(viewProjection*p(i));
      lines[rgb3f(1,0,0)].append(viewProjection*p(j));
     }
    }
  }

  glDepthTest(false);
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
   auto graphics = plots.graphics(vec2f(target.size), Rect(vec2f(target.size)));
   this->lock.unlock();
   render(target, graphics);
   GLTexture image = flip(move(target));
   static GLShader shader {::shader_glsl(), {"blit"}};
   shader.bind();
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   shader["image"] = 0; image.bind(0);
   vec2f min (-1,-1), max (vec2f(target.size)/size*float(2)-vec2f(1));
   GLBuffer positionBuffer (ref<vec2f>{
                  vec2f(min.x,min.y), vec2f(max.x,min.y), vec2f(min.x,max.y),
                  vec2f(min.x,max.y), vec2f(max.x,min.y), vec2f(max.x,max.y)});
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             2, Float, positionBuffer);
   glBlendAlpha();
   vertexArray.draw(Triangles, 6);
  }

  if(encoder) {
   encoder->writeVideoFrame(target.readback());
   if(stop) encoder = nullptr;
  }
  target.blit(0, window->size);
  renderTime.stop();
  if(stop && fitTranslation != this->translation) window->render();

  array<char> s = str(timeStep*this->dt, grain.count, wire.count/*, kT, kR*/
                      /*,staticFrictionCount, dynamicFrictionCount*/);
  if(load.count) s.append(" "_+str(load.position[0].z));
  window->setTitle(s);
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2f cursor, vec2f size, Event event, Button button,
                                Widget*&) override {
  vec2f delta = cursor-lastPos; lastPos=cursor;
  /*if(event==Press && button==WheelDown) {
   stateIndex = clamp(0, stateIndex-1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }
  if(event==Press && button==WheelUp) {
   stateIndex = clamp(0, stateIndex+1, int(states.size-1));
   window->setTitle(str(stateIndex));
   return true;
  }*/
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
   if(encoder) encoder = nullptr;
  }
  else return false;
  return true;
 }
} view;
