#include "window.h"
#include "simulation.h"
#include "grain.h"
#include "membrane.h"
#include "matrix.h"
#include "gl.h"
FILE(shader_glsl)

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Widget {
 const Simulation& simulation;

 size_t timeStep = 0;
 vec2 yawPitch = vec2(0, -PI/2); // Current view angles

 struct {
  vec2 cursor;
  vec2 yawPitch;
  size_t timeStep;
 } dragStart = {0,0,0};

 Time totalTime;
 Time renderTime;

 array<int> grainIndices;
 GLIndexBuffer indexBuffer;
 GLBuffer xBuffer, yBuffer, zBuffer;

 SimulationView(const Simulation& simulation) : simulation(simulation) {}

 vec2 sizeHint(vec2) override { return vec2(1024); }
 shared<Graphics> graphics(vec2 size) override {
  if(!totalTime) totalTime.start();
  renderTime.start();

  vec4 viewRotation = qmul(angleVector(yawPitch.y, vec3(1,0,0)), angleVector(yawPitch.x, vec3(0,0,1)));
  const vec3 scale = vec3(2/(simulation.topZ-simulation.bottomZ));
  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    .scale(scale)
    .rotateX(yawPitch.y).rotateZ(yawPitch.x)
    .translate(vec3(0,0,-(simulation.bottomZ+simulation.topZ)/2));

  glDepthTest(true);

  if(simulation.grain->count) {
   for(int i: range(grainIndices.size, simulation.grain->count)) grainIndices.append(i);
   buffer<float> grainZ (align(simd, grainIndices.size));
   {
    const vXsf Qx = floatX(viewRotation.x);
    const vXsf Qy = floatX(viewRotation.y);
    const vXsf Qz = floatX(viewRotation.z);
    const vXsf Qw = floatX(viewRotation.w);
    const float* pPx = simulation.grain->Px.data+simd, *pPy = simulation.grain->Py.data+simd, *pPz = simulation.grain->Pz.data+simd;
    float* const pQPz = grainZ.begin();
    int size = grainIndices.size;
    for(int i=0; i<size; i+=simd) {
     const vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
     const vXsf X = Qw*Px - Py*Qz + Qy*Pz;
     const vXsf Y = Qw*Py - Pz*Qx + Qz*Px;
     const vXsf Z = Qw*Pz - Px*Qy + Qx*Py;
     const vXsf W = Px * Qx + Py * Qy + Pz * Qz;
     const vXsf QPz = Qw*Z + W*Qz + Qx*Y - X*Qy;
     store(pQPz, i, QPz);
    }
   }
   for(int i: range(1, grainIndices.size)) {
    int j = i;
    while(j > 0 && grainZ[grainIndices[j-1]] > grainZ[grainIndices[j]]) {
     swap(grainIndices[j-1], grainIndices[j]);
     j--;
    }
   }

   buffer<vec3> positions {grainIndices.size*6};
   for(size_t i: range(grainIndices.size)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * simulation.grain->position(grainIndices[i]);
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(simulation.grain->radius) - vec2(2.f/size.x); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(simulation.grain->radius) + vec2(2.f/size.x); // Parallel
    positions[i*6+0] = vec3(min, O.z);
    positions[i*6+1] = vec3(max.x, min.y, O.z);
    positions[i*6+2] = vec3(min.x, max.y, O.z);
    positions[i*6+3] = vec3(min.x, max.y, O.z);
    positions[i*6+4] = vec3(max.x, min.y, O.z);
    positions[i*6+5] = vec3(max, O.z);
   }

   static GLShader shader {::shader_glsl(), {"interleaved sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   /*GLBuffer Rx (simulation.grain->Rx+simd);
   shader.bind("Rx"_, Rx, 0);
   GLBuffer Ry (simulation.grain->Ry+simd);
   shader.bind("Ry"_, Ry, 0);
   GLBuffer Rz (simulation.grain->Rz+simd);
   shader.bind("Rz"_, Rz, 0);
   GLBuffer Rw (simulation.grain->Rw+simd);
   shader.bind("Rw"_, Rw, 0);*/
   // Reduces Z radius to see membrane mesh on/in grain
   shader["radius"] = float(scale.z/2 * simulation.grain->radius * 7/8);
   shader["hpxRadius"] = 1 / (size.x * scale.x * simulation.grain->radius);
   //shader["viewRotation"] = viewRotation;
   vertexArray.draw(Triangles, positions.size);
  }

#if WIRE
  if(simulation.wire.count>1) {
   buffer<vec3> positions {(simulation.wire.count-1)*6};
   buffer<vec4> colors {(simulation.wire.count-1)};
   size_t s = 0;
   for(size_t i: range(simulation.wire.count-1)) {
    vec3 a (simulation.wire.position(i)), b (simulation.wire.position(i+1));
    // FIXME: GPU quad projection
    vec3 A = viewProjection * a, B= viewProjection * b;
    vec2 r = B.xy()-A.xy();
    float l = length(r);
    vec2 t = r/l;
    vec3 n = scale*float(simulation.wire.radius)*vec3(t.y, -t.x, 0); // FIXME: hpx
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
    shader["radius"] = float(scale.z/2 * simulation.wire.radius);
    shader["hpxRadius"] = 1 / (size.x * scale.x * simulation.wire.radius);
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

  if(simulation.membrane->count) {
   if(!indexBuffer) {
    const int W = simulation.membrane->W, stride = simulation.membrane->stride,
      margin = simulation.membrane->margin;
    buffer<uint16> indices(W*(simulation.membrane->H-1)*6-W*2);
    int s = 0;
    for(int i: range(0, simulation.membrane->H-1)) {
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
   shader["transform"] = viewProjection;
   shader["uColor"] = vec4(black, 1);
   static GLVertexArray vertexArray;
   // FIXME: reuse buffers / no update before pressure
   if(!xBuffer) xBuffer = GLBuffer(simulation.membrane->Px);
   else if(simulation.membraneViscosity) xBuffer.upload(simulation.membrane->Px);
   vertexArray.bindAttribute(shader.attribLocation("x"_), 1, Float, xBuffer);
   if(!yBuffer) yBuffer = GLBuffer(simulation.membrane->Py);
   else if(simulation.membraneViscosity) yBuffer.upload(simulation.membrane->Py);
   vertexArray.bindAttribute(shader.attribLocation("y"_), 1, Float, yBuffer);
   if(!zBuffer) zBuffer = GLBuffer(simulation.membrane->Pz);
   else if(simulation.membraneViscosity) zBuffer.upload(simulation.membrane->Pz);
   vertexArray.bindAttribute(shader.attribLocation("z"_), 1, Float, zBuffer);
   vertexArray.bind();
   indexBuffer.draw();
  }

  // Plates
  {
   static GLShader shader {::shader_glsl(), {"interleaved flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["uColor"] = vec4(black, 1);

   size_t N = 64;
   buffer<vec3> positions {N*2*2, 0};
   const int radius = simulation.membrane->radius;
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (radius*cos(a1), radius*sin(a1), simulation.bottomZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (radius*cos(a2), radius*sin(a2), simulation.bottomZ);
    vec3 A = viewProjection * a, B= viewProjection * b;
    positions.append(A); positions.append(B);
   }
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (radius*cos(a1), radius*sin(a1), simulation.topZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (radius*cos(a2), radius*sin(a2), simulation.topZ);
    vec3 A = viewProjection * a, B= viewProjection * b;
    positions.append(A); positions.append(B);
   }
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }
  renderTime.stop();
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
  else return false;
  return true;
 }
};

struct SimulationApp : Poll {
 Simulation simulation;
 SimulationView view;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);
 Thread simulationMasterThread;
 SimulationApp(const Dict& parameters)
  : Poll(0, 0, simulationMasterThread), simulation(parameters), view(simulation) {
  window->actions[Escape] = [this]{ exit_group(0); /*FIXME*/ };
  window->presentComplete = [this]{
   window->render();
   window->setTitle(str(simulation.timeStep*simulation.dt /s, simulation.grain->count, simulation.voidRatio, simulation.maxGrainV /(m/s)));
  };
  queue(); simulationMasterThread.spawn();
 }
 void event() {
  simulation.run();
  window->setTitle("Done");
 }
} app (parameters());
