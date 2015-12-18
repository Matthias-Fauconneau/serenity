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
#if WIRE
  struct Wire {
   float radius = 0;
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  } wire;
#endif
  struct Membrane {
   size_t W, H, stride, margin;
   size_t count = 0;
   buffer<float> Px;
   buffer<float> Py;
   buffer<float> Pz;
   vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  } membrane;
  float radius, bottomZ, topZ;
  vec3 min, max;
 };
 array<State> states;
 Lock lock;

 GLFrameBuffer target;

 size_t timeStep = 0;
 vec2 yawPitch = vec2(0, -PI/3); // Current view angles

 struct {
  vec2 cursor;
  vec2 yawPitch;
  size_t timeStep;
 } dragStart = {0,0,0};

 Time totalTime;
 Time renderTime;
 Time grainTransformTime;
 Time grainTransform2Time;
 Time grainSortTime;
 Time grainTime;
 Time membraneTime;
 Time plateTime;

 array<int> grainIndices;
 GLIndexBuffer indexBuffer;

 vec2 sizeHint(vec2) override { return vec2(1024); }
 shared<Graphics> graphics(vec2 size) override {
  if(!totalTime) totalTime.start();

  vec4 viewRotation = qmul(angleVector(yawPitch.y, vec3(1,0,0)),
                                             angleVector(yawPitch.x, vec3(0,0,1)));

  vec3 scale = 0, translation = 0; // Fit view

  Locker lock(this->lock);
  renderTime.start();
  const State& state = states[timeStep];
  array<vec3> grainPositions (state.grain.count); // Rotated, Z-Sorted
  array<vec4> grainRotations (state.grain.count); // Rotated, Z-Sorted
  {
   const vec3 min = vec3(-state.radius, qapply(viewRotation, vec3(0,0,0)).y, -sqrt(sq(state.topZ/2)+sq(state.radius)));
   const vec3 max = vec3(state.radius, qapply(viewRotation, vec3(0,0,state.topZ)).y, sqrt(sq(state.topZ/2)+sq(state.radius)));
   //struct key_value { float z; int index; bool operator<(const key_value& b) const { return z > b.z; }};
   for(int i: range(grainIndices.size, state.grain.count)) grainIndices.append(i);
   buffer<float> grainZ (align(simd, grainIndices.size));
   grainTransformTime.start();
   const vXsf Qx = floatX(viewRotation.x);
   const vXsf Qy = floatX(viewRotation.y);
   const vXsf Qz = floatX(viewRotation.z);
   const vXsf Qw = floatX(viewRotation.w);
   const float* pPx = state.grain.Px.data, *pPy = state.grain.Py.data, *pPz = state.grain.Pz.data;
   float* const pQPz = grainZ.begin();
   int size = state.grain.count;
   for(int i=0; i<size; i+=simd) {
    const vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
    //vXsf QPx, QPy, QPz;
    //qapply(Qx, Qy, Qz, Qw, Px, Py, Pz, QPx, QPy, QPz);
    const vXsf X = Qw*Px - Py*Qz + Qy*Pz;
    const vXsf Y = Qw*Py - Pz*Qx + Qz*Px;
    const vXsf Z = Qw*Pz - Px*Qy + Qx*Py;
    const vXsf W = Px * Qx + Py * Qy + Pz * Qz;
    const vXsf QPz = Qw*Z + W*Qz + Qx*Y - X*Qy;
    store(pQPz, i, QPz);
   }
   grainTransformTime.stop();
   grainSortTime.start();
   // Checks elements are sorted
   /*if(grainIndices)
    for(int i: range(1, grainIndices.size))
     if(grainZ[grainIndices[i-1]] > grainZ[grainIndices[i]]) { swap(grainIndices[i-1], grainIndices[i]);}*/
   // Sinks elements whose order changed
   for(int i: range(1, grainIndices.size)) {
    int j = i;
    while(j > 0 && grainZ[grainIndices[j-1]] > grainZ[grainIndices[j]]) {
     swap(grainIndices[j-1], grainIndices[j]);
     j--;
    }
   }
   grainSortTime.stop();
   /*for(int i: grainIndices) {
    vec3 O = qapply(viewRotation, state.grain.position(i));
    int j = grainZ.size;
    grainZ.append(O.z);
    while(j > 0 && grainZ[j-1] > O.z) {
     swap(grainZ[j-1], grainZ[j]);
     swap(grainIndices[j-1], grainIndices[j]);
     j--; // Sinks elements whose order changed
    }
   }*/
   //grainTransformTime.stop();
   /*grainTransformTime.start();
   for(int i: range(state.grain.count)) {
    vec3 O = qapply(viewRotation, state.grain.position(i));
    grainIndices.append(key_value{O.z, i});
   }
   grainTransformTime.stop();
   grainSortTime.start();
   sort(grainIndices);
   grainSortTime.stop();*/
   //for(key_value kv: grainIndices) { int index = kv.index;
   grainTransform2Time.start();
   for(int index: grainIndices) { // FIXME: GPU
    grainPositions.append(qapply(viewRotation, state.grain.position(index)));
    grainRotations.append(conjugate(qmul(viewRotation, state.grain.rotation(index))));
   }
   grainTransform2Time.stop();
   scale = vec3(vec2(2/::max(max.x-min.x, max.y-min.y)/1.2), 2/(max-min).z);
   translation = -vec3((min+max).xy()/2.f, min.z);
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
   //buffer<vec4> colors {state.grain.count};
   grainTime.start();
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
   }
   grainTime.stop();

   static GLShader shader {::shader_glsl(), {"interleaved sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   GLBuffer rotationBuffer (grainRotations);
   shader.bind("rotationBuffer"_, rotationBuffer, 0);
   //GLBuffer colorBuffer (colors);
   //shader.bind("colorBuffer"_, colorBuffer, 1);
   shader["radius"] = float(scale.z/2 * state.grain.radius*3/4); // reduce Z radius to see membrane mesh on/in grain
   shader["hpxRadius"] = 1 / (size.x * scale.x * state.grain.radius);
   vertexArray.draw(Triangles, positions.size);
  }

#if WIRE
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
   assert(s*6 <= positions.size);
   positions.size = s*6;
   if(positions.size) {
    static GLShader shader {::shader_glsl(), {"interleaved cylinder"}};
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
#endif

  if(state.membrane.count) {
   if(!indexBuffer) {
    const int W = state.membrane.W, stride = state.membrane.stride, margin=state.membrane.margin;
    buffer<uint16> indices(W*(state.membrane.H-1)*6-W*2);
    int s = 0;
    for(int i: range(1, state.membrane.H-1)) {
     int base = margin+i*stride;
     for(int j: range(W)) {
      int a = base+j;
      int b = base+(j+1)%W;
      int c = base+stride+(j+i%2)%W;
      indices[s+0] = c; indices[s+1] = a;
      indices[s+2] = b; indices[s+3] = c;
      if(i) { indices[s+4] = a; indices[s+5] = b; s += 6;}
      else s += 4;
     }
    }
    indices.size = s;
    indexBuffer = indices;
    indexBuffer.primitiveType = Lines;
   }
   static GLShader shader {::shader_glsl(), {"packed flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = rotatedViewProjection;
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   membraneTime.start();
   GLBuffer xBuffer (state.membrane.Px);
   vertexArray.bindAttribute(shader.attribLocation("x"_), 1, Float, xBuffer);
   GLBuffer yBuffer (state.membrane.Py);
   vertexArray.bindAttribute(shader.attribLocation("y"_), 1, Float, yBuffer);
   GLBuffer zBuffer (state.membrane.Pz);
   vertexArray.bindAttribute(shader.attribLocation("z"_), 1, Float, zBuffer);
   membraneTime.stop();
   vertexArray.bind();
   indexBuffer.draw();
  }

  // Plates
  {
   plateTime.start();
   static GLShader shader {::shader_glsl(), {"interleaved flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["uColor"] = vec4(black, 1);

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
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
   plateTime.stop();
  }
  renderTime.stop();
  if(totalTime.seconds()>1 && renderTime.seconds()*32>totalTime.seconds()) {
   //static bool unused once = ({ log("!Render!:", strD(renderTime, totalTime), "!"); true; });
   log(strD(renderTime, totalTime));
   log("transform", strD(grainTransformTime, renderTime));
   log("transform2", strD(grainTransform2Time, renderTime));
   log("sort", strD(grainSortTime, renderTime));
   log("grain", strD(grainTime, renderTime));
   log("membrane", strD(membraneTime, renderTime));
   log("plate", strD(plateTime, renderTime));
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

struct SimulationApp : Poll {
 Simulation simulation;
 SimulationView view;

 bool running = true;

 Time totalTime, recordTime;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);
 Thread simulationMasterThread;
 SimulationApp(const Dict& parameters) : Poll(0, 0, simulationMasterThread), simulation(parameters) {
  window->actions[Escape] = [this]{ running=false; window=nullptr; exit_group(0); /*FIXME*/ };
  window->presentComplete = [this]{
   if(!running || !simulation.dt) return;
   if(!totalTime) totalTime.start();
   recordTime.start();
   window->setTitle(str( str(simulation.timeStep*simulation.dt, 1u)+"s"_,
                         str(simulation.timeStep*simulation.dt/totalTime.seconds(), 1u)+"x"_,
                         strD(simulation.stepTimeRT, totalTime)));
   record(); view.timeStep=view.states.size-1;
   recordTime.stop();
   window->render();
   if(view.states.size == 8192) { log(view.states.size); window->setTitle("OK"); running = false; }
  };
  view.states.append();
  queue(); simulationMasterThread.spawn();
 }
 void event() {
  if(!simulation.run(totalTime)) {
   window->setTitle("Done");
   running = false;
  } else queue();
 }

 void record() {
  SimulationView::State state;
  state.grain.radius = simulation.grain.radius;
  state.grain.count = simulation.grain.count;
  state.grain.Px = copyRef(simulation.grain.Px.slice(simd, align(simd, state.grain.count)));
  state.grain.Py = copyRef(simulation.grain.Py.slice(simd, align(simd, state.grain.count)));
  state.grain.Pz = copyRef(simulation.grain.Pz.slice(simd, align(simd, state.grain.count)));
  state.grain.Rx = copyRef(simulation.grain.Rx.slice(0, align(simd, state.grain.count)));
  state.grain.Ry = copyRef(simulation.grain.Ry.slice(0, align(simd, state.grain.count)));
  state.grain.Rz = copyRef(simulation.grain.Rz.slice(0, align(simd, state.grain.count)));
  state.grain.Rw = copyRef(simulation.grain.Rw.slice(0, align(simd, state.grain.count)));

#if WIRE
  state.wire.radius = simulation.wire.radius;
  state.wire.count = simulation.wire.count;
  state.wire.Px = copy(simulation.wire.Px);
  state.wire.Py = copy(simulation.wire.Py);
  state.wire.Pz = copy(simulation.wire.Pz);
#endif

  state.membrane.W = simulation.membrane.W;
  state.membrane.H = simulation.membrane.H;
  state.membrane.stride = simulation.membrane.stride;
  state.membrane.margin = simulation.membrane.margin;
  state.membrane.count = simulation.membrane.count;
  state.membrane.Px = copy(simulation.membrane.Px);
  state.membrane.Py = copy(simulation.membrane.Py);
  state.membrane.Pz = copy(simulation.membrane.Pz);

  state.radius = simulation.membrane.radius;
  state.bottomZ = simulation.bottomZ;
  state.topZ = simulation.topZ;
  state.min = simulation.lattice.min;
  state.max = simulation.lattice.max;
  Locker lock(view.lock);
  view.states.last() = ::move(state);
 }
} app (parameters());
