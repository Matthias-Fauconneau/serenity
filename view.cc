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
 unique<Window> window = nullptr;
 // View
 vec2 lastPos; // for relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 2./(32*Grain::radius);
 vec3 translation = 0;
 v4sf rotationCenter = _0f;
 Thread simulationThread {19};
 unique<Encoder> encoder = nullptr;
 GLFrameBuffer target {size};

 SimulationView(Thread& uiThread=mainThread, Dict parameters={parseDict(
    "Friction: 0.1,"
    //"Elasticity:8e6, Rate:300,"
    //"Time step:1e-2,"
    "Time step:1e-3,"
    //"Time step:1e-4,"
    //"Time step:1e-5,"
    //"Pattern:loop,""Height: 0.6, Radius:0.3,"_
    +(validation?"Speed: 0.05,"_:"Speed: 0.1,"_)+
    "Pattern:none,"
    //"Pattern:helix,"
    //"Pattern:cross,"
    //"Pattern:loop,"_
    +(validation?
       //"Height: 0.2, Radius:0.05"_
       //"Height: 0.1, Radius:0.025"_
       "Height: 0.08, Radius:0.02"_
       //"Height: 0.06, Radius:0.016"_
     :"Height: 0.5, Radius:0.2"_)+
    //", Pressure:0"
    //", Pressure: 1e3"
    ", Pressure: "+(validation?"3e6":"1e5")+
    ", Plate Speed: "_+(validation?"3e-6":"1e-4")+
    ", Resolution: 1.4"
    //", Resolution: 1"
    ", G: 10"_
    +(validation?", Validation"_:", Experiment"_)
    )}) : Simulation(parameters,
  arguments().contains("result") ?
   File(str(parameters)+".result", currentWorkingDirectory(),
        Flags(WriteOnly|Create|Truncate)) : File(2/*stdout*/)),
   Poll(0, POLLIN, simulationThread) {
  if(arguments().contains("view")) {
   window = ::window(this, -1, uiThread, true);
   window->actions[F12] = {this, &SimulationView::snapshot};
   window->actions[Return] = [this]{
    skip = true;
    log("skip", int(processState), "grain", grain.count, "wire", wire.count);
   };
  }
  if(arguments().contains("video")) {
   encoder = unique<Encoder>("tas.mp4"_);
   encoder->setH264(int2(1280,720), 60);
   encoder->open();
  }

  mainThread.setPriority(19);
  log(stream.name());
  if(window) {
   queue();
   simulationThread.spawn();
  } else {
   while(processState < Done) {
    if(timeStep%size_t(1e-2/dt) == 0) log(info());
    step();
   }
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
  Simulation::snapshot();
  writeFile(name+".png", encodePNG(target.readback()), currentWorkingDirectory(), true);
 }

 void step() {
  Simulation::step();
  if(encoder) viewYawPitch.x += 2*PI*dt / 16;
  if(window) window->render();
  int64 elapsed = realTime() - lastReport;
  if(elapsed > 30e9 || timeStep > lastReportStep + 2/this->dt) {
   log(timeStep*this->dt, totalTime, (timeStep-lastReportStep) / (elapsed*1e-9), grain.count, wire.count);
   log(str(stepTime, totalTime), "grain",str(grainTime, stepTime), "wire",str(wireTime, stepTime), "side", str(sideTime, stepTime));
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
  else { Simulation::snapshot(); requestTermination(0); }
 }

 String info() {
  array<char> s {
   copyRef(processStates[processState])};
  s.append(" "_+str(grain.count)+"grains"_);
  s.append(" "_+str(int(timeStep*this->dt/**1e3*/))+"s"_);
  //if(processState==Wait)
  s.append(" "_+decimalPrefix(grainKineticEnergy/*/densityScale*//grain.count, "J"));
  if(processState>=ProcessState::Load) {
   s.append(" "_+str(int(plate.position[1][2]*1e3))+"mm");
   float weight = (grain.count*grain.mass + wire.count*wire.mass) * G[2];
   float stress = (plate.force[1][2]-(plate.force[0][2]-weight))/(2*PI*sq(side.initialRadius));
   s.append(" "_+str(int(stress*1e-6))+"MPa");
  }
  if(grain.count) {
   float height = plate.position[1][2] - plate.position[0][2];
   float voidRatio = PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1;
   s.append(" Ratio:"+str(int(voidRatio*100))+"%");
  }
  if(wire.count) {
   float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
   s.append(" Wire density:"+str(int(wireDensity*100))+"%");
  }
  s.append(" Z:"_+str(int(plate.position[1][2]*1e3)));
  s.append(" R:"_+str(int(side.radius*1e3)));
  //float bottom = plate.force[0][2], top = plate.force[1][2];
  //s.append(" "_+str("KN:", int((top+bottom)*1e-3)));
  s.append(" Om%"_+str(int(overlapMean/(2*Grain::radius)*100)));
  s.append(" OM%:"+str(int(overlapMax/(2*Grain::radius)*100)));
  return move(s);
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
    for(auto entry: lines) {
     shader["uColor"] = entry.key;
     GLBuffer positionBuffer (entry.value);
     vertexArray.bindAttribute(shader.attribLocation("position"_),
                               3, Float, positionBuffer);
     vertexArray.draw(Lines, entry.value.size);
    }
   }
   lines.clear();

   if(encoder) {
   encoder->writeVideoFrame(target.readback());
  }
  int offset = (target.size.x-window->size.x)/2;
  target.blit(0, window->size, int2(offset, 0), int2(target.size.x-offset, target.size.y));

  window->setTitle(info());
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

