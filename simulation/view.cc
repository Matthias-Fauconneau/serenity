#include "simulation.h"
#include "matrix.h"
#include "window.h"
#include "time.h"
#include "gl.h"
FILE(shader_glsl)

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Simulation, Widget {
 bool running = true;

 struct State {
  struct Grain {
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   buffer<float> Rx;
   buffer<float> Ry;
   buffer<float> Rz;
   buffer<float> Rw;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
   vec4 rotation(size_t i) const { return vec4(Rx[i], Ry[i], Rz[i], Rw[i]); }
  } grain;
  struct Wire {
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  } wire;
  float radius;
 };
 array<State> states;
 size_t viewT;

 unique<Window> window = nullptr;
 GLFrameBuffer target;

 vec2 lastPos; // For relative cursor movements
 vec2 viewYawPitch = vec2(0, -PI/3); // Current view angles
 vec2 scale = 0;
 vec2 translation = 0;

 Time totalTime, recordTime;

 SimulationView(const Dict& parameters) : Simulation(parameters) {
  window = ::window(this, -1, mainThread, true, false);
  states.append(); viewT=states.size-1;
  window->presentComplete = [this]{
   if(!running) return;
   if(!totalTime) totalTime.start();
   recordTime.start();
   window->setTitle(str( str(timeStep*dt, 1u)+"s"_,
                         str(timeStep*dt/totalTime.seconds(), 1u)+"x"_,
                         strD(stepTimeRT, totalTime)));
   if(!stepProfile(totalTime)) {
    window->setTitle("Error");
    running = false;
   }
   record(); viewT=states.size-1;
   recordTime.stop();
   window->render();
   if(states.size == 8192) { log(states.size); window->setTitle("OK"); running = false; }

  };
 }

 void record() {
  State state;
  state.grain.count = grain.count;
  state.grain.Px = copy(grain.Px);
  state.grain.Py = copy(grain.Py);
  state.grain.Pz = copy(grain.Pz);
  state.grain.Rx = copy(grain.Rx);
  state.grain.Ry = copy(grain.Ry);
  state.grain.Rz = copy(grain.Rz);
  state.grain.Rw = copy(grain.Rw);

  state.wire.count = wire.count;
  state.wire.Px = copy(wire.Px);
  state.wire.Py = copy(wire.Py);
  state.wire.Pz = copy(wire.Pz);

  state.radius = currentSideRadius;
  states.append(::move(state));
 }

 vec2 sizeHint(vec2) override { return vec2(768); }
 shared<Graphics> graphics(vec2 size) override {
  vec4 viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)),
                                             angleVector(viewYawPitch.x, vec3(0,0,1)));

  vec3 scale, translation; // Fit view

  const State& state = states[viewT];
  array<vec3> grainPositions (state.grain.count); // Rotated, Z-Sorted
  array<vec4> grainRotations (state.grain.count); // Rotated, Z-Sorted
  array<size_t> grainIndices (state.grain.count);
  {
   vec3 min = -initialRadius, max = vec3(initialRadius);
   for(size_t i: range(state.grain.count)) {
    vec3 O = qapply(viewRotation, state.grain.position(i));
    min = ::min(min, O - vec3(grain.radius));
    max = ::max(max, O + vec3(grain.radius));
    size_t j = 0;
    while(j < grainPositions.size && grainPositions[j].z < O.z) j++;
    grainPositions.insertAt(j, O);
    grainRotations.insertAt(j, conjugate(qmul(viewRotation, state.grain.rotation(i))));
    grainIndices.insertAt(j, i);
   }
   for(size_t i: range(state.wire.count)) {
    vec3 O = qapply(viewRotation, state.wire.position(i));
    if(O.z > 1) continue;
    min = ::min(min, O - vec3(wire.radius));
    max = ::max(max, O + vec3(wire.radius));
   }
   scale = vec3(vec2(2/::max(max.x-min.x, max.y-min.y)/1.2), 2/(max-min).z);
   translation = -vec3((min+max).xy()/2.f, min.z);
   if(!isNumber(translation.xy())) translation.xy() = this->translation;

   if(!this->scale && !this->translation) this->scale = scale.xy(), this->translation = translation.xy();
   const float Dt = 1./60;
   if(scale.xy() != this->scale || translation.xy() != this->translation) window->render();
   scale.xy() = this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
   translation.xy() = this->translation = this->translation*float(1-Dt) + float(Dt)*translation.xy();
  }

  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    .translate(vec3(0,0,-1))
    .scale(scale)
    .translate(translation);
  mat4 rotatedViewProjection = mat4(viewProjection).rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x);

  if(!target) target = GLFrameBuffer(int2(size));
  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  if(state.grain.count) {
   buffer<vec3> positions {state.grain.count*6};
   buffer<vec4> colors {state.grain.count};
   for(size_t i: range(state.grain.count)) {
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
    colors[i] = vec4(1,1,1,1);
   }

   static GLShader shader {::shader_glsl(), {"sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   GLBuffer rotationBuffer (grainRotations);
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   GLBuffer colorBuffer (colors);
   shader.bind("colorBuffer"_, colorBuffer, 1);
   shader["radius"] = float(scale.z/2 * Grain::radius*3/4); // reduce Z radius to see membrane mesh on/in grain
   shader["hpxRadius"] = 1 / (size.x * scale.x * grain.radius);
   vertexArray.draw(Triangles, positions.size);
  }

  if(state.wire.count>1) {
   buffer<vec3> positions {(state.wire.count-1)*6};
   buffer<vec4> colors {(state.wire.count-1)};
   size_t s = 0;
   for(size_t i: range(state.wire.count-1)) {
    vec3 a (state.wire.position(i)), b (state.wire.position(i+1));
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
    colors[s] = vec4(1, 1, 1, 1);
    s++;
   }
   assert_(s*6 <= positions.size);
   positions.size = s*6;
   if(positions.size) {
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
    GLBuffer colorBuffer (colors);
    shader.bind("colorBuffer"_, colorBuffer, 1);
    vertexArray.draw(Triangles, positions.size);
   }
  }

  // Radius
  {
   static GLShader shader {::shader_glsl(), {"flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);

   size_t N = 64;
   buffer<vec3> positions {N*2};
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (state.radius*cos(a1), state.radius*sin(a1), 0);
    float a2 = 2*PI*(i+1)/N; vec3 b (state.radius*cos(a2), state.radius*sin(a2), 0);
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    positions[i*2+0] = A; positions[i*2+1] = B;
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
    viewYawPitch.y = clamp<float>(-PI, viewYawPitch.y, 0);
   }
   else if(event==Motion && button==RightButton) {
    viewT = clamp<int>(0, int(dragStart.viewT) + (states.size-1) * (cursor.x - dragStart.cursor.x) / (size.x/2-1), states.size-1); // Relative
    scale = 0; translation = 0;
    running = false;
   }
   else return false;
   return true;
 }
} app (parameters());
