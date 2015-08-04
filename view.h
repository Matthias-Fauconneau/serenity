#include "simulation.h"
#include "matrix.h"
#if UI
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
#include "gl.h"
FILE(shader_glsl)
#endif

template<Type tA> vec4f toGlobal(tA& A, size_t a, vec4f localA) {
 return A.position[a] + qapply(A.rotation[a], localA);
}

/*struct StandardInput : Poll {
 function<void(string)> input;
 StandardInput(function<void(string)> input) : input(input) { registerPoll(); }
 void event() override { input(Stream(0).readUpTo(1<<16)); }
};*/

struct SimulationRun : Simulation {
 //StandardInput stdin {[this](string){ Simulation::snapshot(); }};
 SimulationRun(const Dict& parameters, File&& file) : Simulation(parameters, move(file)) {
  log(id);
  if(existsFile(id+".result")) log("Existing result", id);
  /*extern function<void(int)> userSignal;
  userSignal = [this](int) { Simulation::snapshot(); };*/
#if !UI
  Time time (true);
  while(processState < Done) {
   if(timeStep%size_t(1e-1/dt) == 0) {
    if(processState  < Load && timeStep*dt > 6*60) { log("6min limit"); break; }
    if(processState  < Load && time.toReal() > 16*60*60) { log("16h limit"); break; }
    report();
    log(info());
   }
   step();
  }
  if(processState != ProcessState::Done) {
   log("Failed");
   if(existsFile(id+".failed")) remove(id+".failed", currentWorkingDirectory());
   rename(id+".working", id+".failed", currentWorkingDirectory());
  } else {
   rename(id+".working", id+".result", currentWorkingDirectory());
  }
  log(time);
#endif
 }

 void report() {
  int64 elapsed = realTime() - lastReport;
  log(timeStep*dt, totalTime, (timeStep-lastReportStep) / (elapsed*1e-9), grain.count, wire.count);
  log("grain",str(grainTime, stepTime),
      "grainInit",str(grainInitializationTime, stepTime),
      "grainLattice",str(grainGridTime, stepTime),
      "grainContact",str(grainContactTime, stepTime),
      "grainIntegration",str(grainIntegrationTime, stepTime));
  if(wire.count) log("wire",str(wireTime, stepTime),
                     "wireLatticeTime",str(wireLatticeTime, stepTime),
                     "wireContact",str(wireContactTime, stepTime),
                     "wireIntegration",str(wireIntegrationTime, stepTime));
  log("side", str(sideTime, stepTime),
      "sideGrid",str(sideGridTime, stepTime),
      "sideForce",str(sideForceTime, stepTime),
      "sideIntegration",str(sideIntegrationTime, stepTime));
  lastReport = realTime();
  lastReportStep = timeStep;
 }
};

#if UI
struct SimulationView : SimulationRun, Widget, Poll {
 int2 size {/*1280,720*/1050};
 unique<Window> window = nullptr;
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 2./(32*Grain::radius);
 vec3 translation = 0;
 v4sf rotationCenter = _0f;
 Thread simulationThread {19};
#if ENCODER
 unique<Encoder> encoder = nullptr;
#endif
 GLFrameBuffer target;
 size_t lastTitleSetStep = 0;

 SimulationView(const Dict& parameters, File&& file) :
   SimulationRun(parameters, move(file)),
   Poll(0, POLLIN, simulationThread) {
  if(/*XDisplay::hasServer() &&*/ /*arguments().contains("view")*/1) {
   window = ::window(this, -1, mainThread, true);
   window->actions[F11] = {(SimulationRun*)this, &SimulationRun::report};
   window->actions[F12] = {this, &SimulationView::snapshot};
   window->actions[Return] = [this]{
    skip = true;
    log("skip", int(processState), "grain", grain.count, "wire", wire.count);
   };
  }
#if ENCODER
  if(arguments().contains("video")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }
#endif

  mainThread.setPriority(19);
  if(window) {
   queue();
   simulationThread.spawn();
  }
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
  //Simulation::snapshot();
  log("View::snapshot");
  writeFile(name+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }

 void step() {
  Simulation::step();
#if ENCODER
  if(encoder) viewYawPitch.x += 2*PI*dt / 16;
#endif
  if(window) window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 60e9 || timeStep > lastReportStep + 8/this->dt) {
   report();
#if PROFILE
   requestTermination();
#endif
  }
  if(timeStep > lastTitleSetStep+size_t(0.1/dt)) { lastTitleSetStep=timeStep; window->setTitle(info()); }
  if(processState < Done) queue();
  else if(processState == Done) { /*Simulation::snapshot();*/ log("Done"); requestTermination(0); }
  else {
   window->setTitle(info());
   log(plate.position[0], plate.position[1]);
   window->show(); // Raises
   extern int groupExitStatus;
   groupExitStatus = -1; // Let user view and return failed exit status on window close to stop sweep
  }
 }

 vec2 sizeHint(vec2) override { return vec2(1050, 1050*size.y/size.x); }
 shared<Graphics> graphics(vec2) override {
  const float Dt = 1./60/2;
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
    min = ::min(min, O - vec3(vec2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(Grain::radius), 0)); // Parallel
   }
   for(size_t i: range(wire.count)) {
    vec3 O = toVec3(qapply(viewRotation, wire.position[i]) - rotationCenter);
    min = ::min(min, O - vec3(vec2(Grain::radius), 0)); // Parallel
    max = ::max(max, O + vec3(vec2(Grain::radius), 0)); // Parallel
   }
  }

  if(!target) target = GLFrameBuffer(this->size);
  vec2 size (target.size.x, target.size.y);
  vec2 viewSize = size;

  vec3 scaleMax = max; ::min(max, vec3(vec2(16*Grain::radius), 32*Grain::radius));
  vec3 scaleMin = min; ::max(min, vec3(vec2(-16*Grain::radius), 0));
  vec3 scale (2*::min(viewSize/(scaleMax-scaleMin).xy())/size,
                    -1/(2*(max-min).z));
  this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
  //this->scale = /*this->scale*float(1-Dt) + float(Dt)**/scale.xy();
  scale.xy() = this->scale;

  vec3 translation = this->translation = vec3((size-viewSize)/size, 0);

  mat4 viewProjection = mat4()
    .translate(translation)
    .scale(scale)
    .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
    .translate(-toVec3(rotationCenter));

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
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   GLBuffer rotationBuffer (apply(grain.rotation.slice(0, grain.count),
                [=](vec4f q) -> vec4f { return conjugate(qmul(viewRotation,q)); }));
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   shader["radius"] = float(scale.z/2 * Grain::radius*3/4); // reduce Z radius to see membrane mesh on/in grain
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
   shader["transform"] = mat4(1);
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
  shader["transform"] = mat4(1);

  if(side.faceCount>1) {Locker lock(this->lock);
   size_t W = side.W;
   buffer<vec3> positions {W*(side.H-1)*6-W*2};
   for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    vec3 a (toVec3(side.Vertex::position[i*W+j]));
    vec3 b (toVec3(side.Vertex::position[i*W+(j+1)%W]));
    vec3 c (toVec3(side.Vertex::position[(i+1)*W+(j+i%2)%W]));
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

  {
   Locker lock(this->lock);
   size_t W = side.W;
   buffer<vec3> positions {W*2*2};
   for(size_t i: range(2)) for(size_t j: range(W)) {
    float z = plate.position[0][2] + i * (plate.position[1][2]-plate.position[0][2]);
    float a1 = 2*PI*(j+0)/W; vec3 a (side.radius*cos(a1), side.radius*sin(a1), z);
    float a2 = 2*PI*(j+1)/W; vec3 b (side.radius*cos(a2), side.radius*sin(a2), z);
    // FIXME: GPU projection
    vec3 A = viewProjection * a, B= viewProjection * b;
    size_t n = (i*W+j)*2;
    positions[n+0] = A; positions[n+1] = B;
   }
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

   if(1) glDepthTest(false);
   {static GLVertexArray vertexArray;
    shader["transform"] = viewProjection;
    Locker lock(this->lock);
    for(auto entry: lines) {
     shader["uColor"] = vec4(entry.key, 1);
     GLBuffer positionBuffer (entry.value);
     vertexArray.bindAttribute(shader.attribLocation("position"_),
                               3, Float, positionBuffer);
     vertexArray.draw(Lines, entry.value.size);
    }
   }
   //lines.clear();
#if ENCODER
   if(encoder) encoder->writeVideoFrame(target.readback());
#endif
  int offset = (target.size.x-window->size.x)/2;
  target.blit(0, window->size, int2(offset, 0), int2(target.size.x-offset, target.size.y));

  if(timeStep > lastTitleSetStep+size_t(::max(1., 0.008/dt))) { lastTitleSetStep=timeStep; window->setTitle(info()); }
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
  vec2 delta = cursor-lastPos; lastPos=cursor;
  if(event==Motion && button==LeftButton) {
   viewYawPitch += float(2*PI) * delta / size; //TODO: warp
   viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
#if ENCODER
   if(encoder) encoder = nullptr;
#endif
  }
  else return false;
  return true;
 }
};
#endif
