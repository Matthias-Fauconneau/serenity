#include "window.h"
#include "simulation.h"
#include "matrix.h"
#include "gl.h"
FILE(shader_glsl)

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Widget {
 struct State {
  struct Grain {
   float radius = 0;
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
   float radius = 0;
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  } wire;
  struct Membrane {
   size_t W, H, stride;
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  } membrane;
  float radius, bottomZ, topZ;
 };
 array<State> states;

 GLFrameBuffer target;

 size_t timeStep = 0;
 vec2 yawPitch = vec2(0, -PI/3); // Current view angles

 struct {
  vec2 cursor;
  vec2 yawPitch;
  size_t timeStep;
 } dragStart = {0,0,0};

 vec2 sizeHint(vec2) override { return vec2(768); }
 shared<Graphics> graphics(vec2 size) override {
  vec4 viewRotation = qmul(angleVector(yawPitch.y, vec3(1,0,0)),
                                             angleVector(yawPitch.x, vec3(0,0,1)));

  vec3 scale = 0, translation = 0; // Fit view

  const State& state = states[timeStep];
  array<vec3> grainPositions (state.grain.count); // Rotated, Z-Sorted
  array<vec4> grainRotations (state.grain.count); // Rotated, Z-Sorted
  array<size_t> grainIndices (state.grain.count);
  {
   vec3 min = 0, max = 0; //-state.membrane.radius, max = vec3(state.membrane.radius);
   for(size_t i: range(state.grain.count)) {
    vec3 O = qapply(viewRotation, state.grain.position(i));
    min = ::min(min, O - vec3(state.grain.radius));
    max = ::max(max, O + vec3(state.grain.radius));
    size_t j = 0;
    while(j < grainPositions.size && grainPositions[j].z < O.z) j++;
    grainPositions.insertAt(j, O);
    grainRotations.insertAt(j, conjugate(qmul(viewRotation, state.grain.rotation(i))));
    grainIndices.insertAt(j, i);
   }
   for(size_t i: range(state.wire.count)) {
    vec3 O = qapply(viewRotation, state.wire.position(i));
    if(O.z > 1) continue;
    min = ::min(min, O - vec3(state.wire.radius));
    max = ::max(max, O + vec3(state.wire.radius));
   }
   for(size_t i: range(state.membrane.H-1)) for(size_t j: range(state.membrane.W)) {
    vec3 O = qapply(viewRotation, state.membrane.position(simd+i*state.membrane.stride+j));
    if(O.z > 1) continue;
    min = ::min(min, O);
    max = ::max(max, O);
   }
   scale = vec3(vec2(2/::max(max.x-min.x, max.y-min.y)/1.2), 2/(max-min).z);
   translation = -vec3((min+max).xy()/2.f, min.z);
   /*if(!isNumber(translation.xy())) translation.xy() = this->translation;
   if(!this->scale && !this->translation) this->scale = scale.xy(), this->translation = translation.xy();
   const float Dt = 1./60;
   if(scale.xy() != this->scale || translation.xy() != this->translation) window->render();
   scale.xy() = this->scale = this->scale*float(1-Dt) + float(Dt)*scale.xy();
   translation.xy() = this->translation = this->translation*float(1-Dt) + float(Dt)*translation.xy();*/
  }

  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    .translate(vec3(0,0,-1))
    .scale(scale)
    .translate(translation);
  mat4 rotatedViewProjection = mat4(viewProjection).rotateX(yawPitch.y) .rotateZ(yawPitch.x);

  if(!target) target = GLFrameBuffer(int2(size));
  target.bind(ClearColor|ClearDepth);
  glDepthTest(true);

  if(state.grain.count) {
   buffer<vec3> positions {state.grain.count*6};
   buffer<vec4> colors {state.grain.count};
   for(size_t i: range(state.grain.count)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * grainPositions[i];
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(state.grain.radius) - vec2(2.f/size.x); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(state.grain.radius) + vec2(2.f/size.x); // Parallel
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
   shader["radius"] = float(scale.z/2 * state.grain.radius*3/4); // reduce Z radius to see membrane mesh on/in grain
   shader["hpxRadius"] = 1 / (size.x * scale.x * state.grain.radius);
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
    vec3 n = scale*float(state.wire.radius)*vec3(t.y, -t.x, 0); // FIXME: hpx
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
    shader["radius"] = float(scale.z/2 * state.wire.radius);
    shader["hpxRadius"] = 1 / (size.x * scale.x * state.wire.radius);
    static GLVertexArray vertexArray;
    GLBuffer positionBuffer (positions);
    vertexArray.bindAttribute(shader.attribLocation("position"_),
                              3, Float, positionBuffer);
    GLBuffer colorBuffer (colors);
    shader.bind("colorBuffer"_, colorBuffer, 1);
    vertexArray.draw(Triangles, positions.size);
   }
  }

  if(state.membrane.count) {
   static GLShader shader {::shader_glsl(), {"color"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = rotatedViewProjection;

   const size_t W = state.membrane.W, stride = state.membrane.stride;
   buffer<vec3> positions {W*(state.membrane.H-1)*6-W*2};
   size_t s = 0;
   for(size_t i: range(state.membrane.H-1)) for(size_t j: range(W)) {
    vec3 a (state.membrane.position(simd+i*stride+j));
    vec3 b (state.membrane.position(simd+i*stride+(j+1)%W));
    vec3 c (state.membrane.position(simd+(i+1)*stride+(j+i%2)%W));
    // FIXME: GPU projection
    vec3 A = a, B = b, C = c;
    positions[s+0] = C; positions[s+1] = A;
    positions[s+2] = B; positions[s+3] = C;
    if(i) { positions[s+4] = A; positions[s+5] = B; s += 6; }
    else s += 4;
   }
   assert_(s <= positions.size && s, state.membrane.W, state.membrane.H);
   positions.size = s;
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  // Bottom
  {
   static GLShader shader {::shader_glsl(), {"flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);

   size_t N = 64;
   buffer<vec3> positions {N*2*2, 0};
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (state.radius*cos(a1), state.radius*sin(a1), state.bottomZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (state.radius*cos(a2), state.radius*sin(a2), state.bottomZ);
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    positions.append(A); positions.append(B);
   }
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (state.radius*cos(a1), state.radius*sin(a1), state.topZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (state.radius*cos(a2), state.radius*sin(a2), state.topZ);
    vec3 A = rotatedViewProjection * a, B= rotatedViewProjection * b;
    positions.append(A); positions.append(B);
   }
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  int offset = 0; //(target.size.x-window->size.x)/2;
  target.blit(0, /*window->size*/target.size, int2(offset, 0), int2(target.size.x-offset, target.size.y));
  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                Widget*&) override {
   if(event == Press) dragStart = {cursor, yawPitch, timeStep};
   if(event==Motion && button==LeftButton) {
    yawPitch = dragStart.yawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
    yawPitch.y = clamp<float>(-PI, yawPitch.y, 0);
   }
   else if(event==Motion && button==RightButton) {
    timeStep = clamp<int>(0,
                      int(dragStart.timeStep) + (states.size-1) * (cursor.x - dragStart.cursor.x) / (size.x/2-1),
                       states.size-1);
   }
   else return false;
   return true;
 }
};

struct SimulationApp {
 Simulation simulation;
 SimulationView view;

 bool running = true;

 Time totalTime, recordTime;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);
 SimulationApp(const Dict& parameters) : simulation(parameters) {
  record(); view.timeStep=view.states.size-1;
  window->presentComplete = [this]{
   if(!running || !simulation.dt) return;
   if(!totalTime) totalTime.start();
   recordTime.start();
   window->setTitle(str( str(simulation.timeStep*simulation.dt, 1u)+"s"_,
                         str(simulation.timeStep*simulation.dt/totalTime.seconds(), 1u)+"x"_,
                         strD(simulation.stepTimeRT, totalTime)));
   if(!simulation.run(totalTime)) {
    window->setTitle("Done");
    running = false;
   }
   record(); view.timeStep=view.states.size-1;
   recordTime.stop();
   window->render();
   if(view.states.size == 8192) { log(view.states.size); window->setTitle("OK"); running = false; }
  };
 }

 void record() {
  SimulationView::State state;
  state.grain.radius = simulation.grain.radius;
  state.grain.count = simulation.grain.count;
  state.grain.Px = copy(simulation.grain.Px);
  state.grain.Py = copy(simulation.grain.Py);
  state.grain.Pz = copy(simulation.grain.Pz);
  state.grain.Rx = copy(simulation.grain.Rx);
  state.grain.Ry = copy(simulation.grain.Ry);
  state.grain.Rz = copy(simulation.grain.Rz);
  state.grain.Rw = copy(simulation.grain.Rw);

  state.wire.radius = simulation.wire.radius;
  state.wire.count = simulation.wire.count;
  state.wire.Px = copy(simulation.wire.Px);
  state.wire.Py = copy(simulation.wire.Py);
  state.wire.Pz = copy(simulation.wire.Pz);

  state.membrane.W = simulation.membrane.W;
  state.membrane.H = simulation.membrane.H;
  state.membrane.stride = simulation.membrane.stride;
  state.membrane.count = simulation.membrane.count;
  state.membrane.Px = copy(simulation.membrane.Px);
  state.membrane.Py = copy(simulation.membrane.Py);
  state.membrane.Pz = copy(simulation.membrane.Pz);

  state.radius = simulation.membrane.radius;
  state.bottomZ = simulation.bottomZ;
  state.topZ = simulation.topZ;
  view.states.append(::move(state));
 }
} app (parameters());
