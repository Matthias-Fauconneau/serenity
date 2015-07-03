#include "simulation.h"
#include "matrix.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "gl.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
FILE(shader_glsl)

template<Type tA> vec4f toGlobal(tA& A, size_t a, vec4f localA) {
 return A.position[a] + qapply(A.rotation[a], localA);
}

struct SimulationView : Simulation, Widget, Poll {
 int2 size {/*1280,720*/1050};
 unique<Window> window;
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 2./(32*Grain::radius);
 vec3 translation = 0;
 vec3 rotationCenter = 0;
 Thread simulationThread {19};
 unique<Encoder> encoder = nullptr;
 GLFrameBuffer target {size};

 SimulationView(Thread& uiThread=mainThread, Dict parameters={parseDict(
    "Pattern:none,"
    //"Pattern:helix,"
    //"Pattern:cross,"
    //"Pattern:loop,"
    "Friction:1, Elasticity:8e6, Rate:200,"
    //"Time step:1e-5, Height: 0.6, Radius:0.3"
    "Time step:1e-5, Height: 0.3, Radius:0.15, Pressure:0"
    )}) : Simulation(parameters,
  arguments().contains("result") ?
   File(str(parameters)+".result", currentWorkingDirectory(),
        Flags(WriteOnly|Create|Truncate)) : File(2/*stdout*/)),
   Poll(0, POLLIN, simulationThread), window(::window(this, -1, uiThread, true)) {
  window->actions[F12] = {this, &SimulationView::snapshot};
  window->actions[Return] = [this]{
   if(processState < Done) processState++;
   log(int(processState), "grain", grain.count, "wire", wire.count);
  };
  if(arguments().contains("video")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

  mainThread.setPriority(19);
  simulationThread.spawn();
  queue();
 }
 ~SimulationView() {
  log("~", "grain", grain.count, "wire", wire.count);
 }

 void event() {
#if PROFILE
  static unused bool once =
    ({ extern void profile_reset(); profile_reset(); true; });
#endif
  step();
 }

 void snapshot() override {
  Simulation::snapshot();
  writeFile(name+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }

 void step() {
  Simulation::step();
  if(encoder) viewYawPitch.x += 2*PI*dt / 16;
  window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 12*60e9) {
   log(timeStep*this->dt, totalTime, (timeStep-lastReportStep) / (elapsed*1e-9), grain.count, wire.count);
   log("grain",str(grainTime, stepTime), "wire",str(wireTime, stepTime));
   log("grainInit",str(grainInitializationTime, grainTime),
       "grainLattice",str(grainLatticeTime, grainTime),
       "grainContact",str(grainContactTime, grainTime),
       "grainIntegration",str(grainIntegrationTime, grainTime));
   log("wireLatticeTime",str(wireLatticeTime, wireTime),
       "wireContact",str(wireContactTime, wireTime),
       "wireIntegration",str(wireIntegrationTime, wireTime));
   lastReport = realTime();
   lastReportStep = timeStep;
#if PROFILE
   requestTermination();
#endif
  }
  if(processState < Done) queue();
  else { snapshot(); requestTermination(0); }
 }

 vec2 sizeHint(vec2) override { return vec2(1050, 1050*size.y/size.x); }
 shared<Graphics> graphics(vec2) override {
  const float Dt = 1./60;
  {
   vec4f min = _0f, max = _0f;
   Locker lock(this->lock);
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

  if(1) {Locker lock(this->lock);
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

  if(1) {Locker lock(this->lock);
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

  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});

  {Locker lock(this->lock);
   size_t W = side.W;
   buffer<vec3> positions {W*(side.H-1)*6-W*2};
   for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    vec3 a (toVec3(side.Particle::position[i*W+j]));
    vec3 b (toVec3(side.Particle::position[i*W+(j+1)%W]));
    vec3 c (toVec3(side.Particle::position[(i+1)*W+(j+i%2)%W]));
    // FIXME: GPU projection
    vec3 A = viewProjection * a, B= viewProjection * b, C = viewProjection * c;
    size_t n = i ? W*4 + ((i-1)*W+j)*6 : j*4;
    positions[n+0] = C; positions[n+1] = A;
    positions[n+2] = B; positions[n+3] = C;
    if(i) { positions[n+4] = A; positions[n+5] = B; }
   }
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  /*{Locker lock(this->lock);
     buffer<vec3> positions {side.faceCount*3, 0};
     for(size_t faceIndex: range(side.faceCount)) {
      if(!flag2.contains(faceIndex)) continue;
      size_t W = side.W;
      size_t i = faceIndex/2/W, j = (faceIndex/2)%W;
      vec3 a (toVec3(side.Particle::position[i*W+j]));
      vec3 b (toVec3(side.Particle::position[i*W+(j+1)%W]));
      vec3 c (toVec3(side.Particle::position[(i+1)*W+j]));
      vec3 d (toVec3(side.Particle::position[(i+1)*W+(j+1)%W]));
      vec3 vertices[2][2][3] {{{a,b,c},{b,d,c}},{{a,d,c},{a,b,d}}};
      vec3* V = vertices[i%2][faceIndex%2];
      // FIXME: GPU projection
      for(size_t i: range(3)) positions[positions.size+i] = viewProjection * V[i];
      positions.size += 3;
     }
     if(positions.size) {
      shader["uColor"] = vec4(1,0,0,1./2);
      static GLVertexArray vertexArray;
      GLBuffer positionBuffer (positions);
      vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
      glBlendAlpha();
      glCullFace(true);
      vertexArray.draw(Triangles, positions.size);
     }
    }*/

  if(encoder) {
   encoder->writeVideoFrame(target.readback());
  }
  int offset = (target.size.x-window->size.x)/2;
  target.blit(0, window->size, int2(offset, 0), int2(target.size.x-offset, target.size.y));

  array<char> s {
   copyRef(processStates[processState])};
  s.append(" "_+str(grain.count)+" grains"_);
  s.append(" "_+str(int(timeStep*this->dt*1000))+"ms"_);
  //if(processState==Wait)
  s.append(" "_+str(int(grainKineticEnergy*1e6/grain.count))+"µJ");
  if(processState>=ProcessState::Load) {
   s.append(" "_+str(int(load.height*1000))+"mm");
   s.append(" "_+str(int(load.force[0][2]))+"N");
  }
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
} app;

