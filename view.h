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

struct SimulationRun : Simulation {
 SimulationRun(const Dict& parameters, File&& file) : Simulation(parameters, move(file)) {
  log(id);
  if(existsFile(id+".result")) log("Existing result", id);
#if !UI
  Time time (true);
  if(existsFile(id)) remove(id);
  //File input(id, currentWorkingDirectory(), Flags(ReadOnly|Create)); //Stream(0)
  //int64 lastTime = input.modifiedTime();

  while(processState < Done) {
   if(timeStep%size_t(1e-1/dt) == 0) {
    //if(processState  < Load && timeStep*dt > 6*60) { log("6min limit"); break; }
    //if(processState  < Load && time.toReal() > 24*60*60) { log("24h limit"); break; }
    report();
    log(info());
   }
   /*int64 time = input.modifiedTime();
   if(time > lastTime) { snapshot("signal"); input.touch(); lastTime=input.modifiedTime(); } // Touch the file for snapshot*/
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
  assert_(mainThread.size==1);
  exit_group(0); // FIXME
#endif
 }

 void report() {
  float height = plate.position[1][2]-plate.position[0][2];
  log(timeStep*dt, 1-height/(topZ0-bottomZ0));
  if(grainSideTime) log("grain-side check", strD(grainSideTime-grainSideForceTime, stepTime));
  if(grainSideForceTime) log("grain-side force", strD(grainSideForceTime, stepTime));
  log("grain-side lattice", str(grainSideLatticeTime, stepTime));
  if(sideForceTime) log("side force", str(sideForceTime, stepTime));
  //log("grain-grain check", strD(grainGrainTime-grainGrainForceTime, stepTime));
  //log("grain-grain force", strD(grainGrainForceTime, stepTime));
  log("grain-grain", strD(grainGrainTime, stepTime));
  log("integration", strD(integrationTime, stepTime));
  //log("process", str(processTime, stepTime));
  //log("grain", str(grainTime, stepTime));
  //log("grain side", str(grainSideTime, stepTime));
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
 v4sf rotationCenter = _0f4;
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
   window = ::window(this, -1, mainThread, true, false);
   window->actions[F11] = {(SimulationRun*)this, &SimulationRun::report};
   window->actions[F12] = [this](){ snapshot("ui"); };
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

 void event() override {
#if PROFILE
  static unused bool once =
    ({ extern void profile_reset(); profile_reset(); true; });
#endif
  step();
 }

 /*void snapshot() override {
  Simulation::snapshot();
  log("View::snapshot");
  writeFile(name+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }*/

 void step() {
  Simulation::step();
#if ENCODER
  if(encoder) viewYawPitch.x += 2*PI*dt / 16;
#endif
  if(window) window->render();
  //int64 elapsed = realTime() - lastReport;
  if(/*elapsed > 10e9 ||*/ timeStep > lastReportStep + 1/this->dt) {
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
   window->show(); // Raises
   extern int groupExitStatus;
   groupExitStatus = -1; // Let user view and return failed exit status on window close to stop sweep
  }
 }

 vec2 sizeHint(vec2) override { return vec2(1050, 1050*size.y/size.x); }
 shared<Graphics> graphics(vec2) override {
  size_t grainCount;
  {Locker lock(this->lock); grainCount = grain.count;}

  vec4f viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 scale, translation; // Fit view
  array<vec3> grainPositions (grain.count); // Rotated, Z-Sorted
  array<vec4f> grainRotations (grain.count); // Rotated, Z-Sorted
  {
   vec3 min = inf, max = -inf;
   for(size_t i: range(grainCount)) {
    vec3 O = toVec3(qapply(viewRotation, grain.position[i]));
    min = ::min(min, O - vec3(grain.radius));
    max = ::max(max, O + vec3(grain.radius));
    size_t j = 0;
    while(j < grainPositions.size && grainPositions[j].z < O.z) j++;
    grainPositions.insertAt(j, O);
    grainRotations.insertAt(j, conjugate(qmul(viewRotation, grain.rotation[i])));
   }
   scale = vec3(vec2(2/::max(max.x-min.x, max.y-min.y)), 2/(max-min).z);
   translation = -vec3((min+max).xy()/2.f, min.z);
  }

  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    .translate(vec3(0,0,-1))
    .scale(scale)
    .translate(translation);
  mat4 rotatedViewProjection = mat4(viewProjection).rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x);

  if(!target) target = GLFrameBuffer(this->size);
  vec2 size (target.size.x, target.size.y);

  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  if(grainCount) {
   buffer<vec3> positions {grainCount*6};
   for(size_t i: range(grainCount)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * grainPositions[i];
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(Grain::radius) - vec2(2.f/size.x); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(Grain::radius) + vec2(2.f/size.x); // Parallel
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
   Locker lock(this->lock);
   GLBuffer rotationBuffer (grainRotations);
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   shader["radius"] = float(scale.z/2 * Grain::radius*3/4); // reduce Z radius to see membrane mesh on/in grain
   shader["hpxRadius"] = 1 / (size.x * scale.x * grain.radius);
   vertexArray.draw(Triangles, positions.size);
  }

  if(wire.count>1) {
   size_t wireCount = wire.count;
   buffer<vec3> positions {(wireCount-1)*6};
   size_t s = 0;
   for(size_t i: range(wireCount-1)) {
    vec3 a (toVec3(wire.position[i])), b (toVec3(wire.position[i+1]));
    if(length(a.xy()) > 0.05) continue;
    if(length(b.xy()) > 0.05) continue;
    // FIXME: GPU quad projection
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    vec2 r = B.xy()-A.xy();
    float l = length(r);
    vec2 t = r/l;
    vec3 n = scale*float(Wire::radius)*vec3(t.y, -t.x, 0); // FIXME: hpx
    vec3 P[4] {A-n, A+n, B-n, B+n};
    positions[s*6+0] = P[0];
    positions[s*6+1] = P[1];
    positions[s*6+2] = P[2];
    positions[s*6+3] = P[2];
    positions[s*6+4] = P[1];
    positions[s*6+5] = P[3];
    s++;
   }
   assert_(s*6 <= positions.size);
   positions.size = s*6;
   static GLShader shader {::shader_glsl(), {"cylinder"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["radius"] = float(scale.z/2 * Wire::radius);
   shader["hpxRadius"] = 1 / (size.x * scale.x * wire.radius);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_),
                             3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }

  // Membrane
  {
   static GLShader shader {::shader_glsl(), {"color"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = rotatedViewProjection;

   // World space bounding box
   /*vec3 min = inf, max = -inf;
   for(size_t i: range(grainCount)) {
    min = ::min(min, toVec3(grain.position[i]) - vec3(grain.radius));
    max = ::max(max, toVec3(grain.position[i]) + vec3(grain.radius));
   }*/

   size_t W = side.W, stride=side.stride;
   buffer<vec3> positions {W*(side.H-1)*6-W*2};
   size_t s = 0;
   for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    vec3 a (toVec3(side.Vertex::position[7+i*stride+j]));
    vec3 b (toVec3(side.Vertex::position[7+i*stride+(j+1)%W]));
    vec3 c (toVec3(side.Vertex::position[7+(i+1)*stride+(j+i%2)%W]));
    /*if(a.z < min.z || a.z > max.z) continue;
    if(b.z < min.z || b.z > max.z) continue;
    if(c.z < min.z || c.z > max.z) continue;*/
    // FIXME: GPU projection
    vec3 A =  a, B=  b, C =  c;
    positions[s+0] = C; positions[s+1] = A;
    positions[s+2] = B; positions[s+3] = C;
    if(i) { positions[s+4] = A; positions[s+5] = B; s += 6; }
    else s += 4;
   }
   assert_(s <= positions.size);
   positions.size = s;
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   /*buffer<vec3> colors (positions.size); colors.clear(0);
   GLBuffer colorBuffer (colors);
   vertexArray.bindAttribute(shader.attribLocation("color"_), 3, Float, colorBuffer);*/
   vertexArray.draw(Lines, positions.size);
  }

  static GLShader shader {::shader_glsl(), {"flat"}};
  shader.bind();
  shader.bindFragments({"color"});
  shader["transform"] = mat4(1);

  // Plates
  {
   size_t W = side.W;
   buffer<vec3> positions {W*2*2};
   for(size_t i: range(2)) for(size_t j: range(W)) {
    float z = plate.position[0][2] + i * (plate.position[1][2]-plate.position[0][2]);
    float a1 = 2*PI*(j+0)/W; vec3 a (side.radius*cos(a1), side.radius*sin(a1), z);
    float a2 = 2*PI*(j+1)/W; vec3 b (side.radius*cos(a2), side.radius*sin(a2), z);
    // FIXME: GPU projection
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
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
    shader["transform"] = rotatedViewProjection;
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
