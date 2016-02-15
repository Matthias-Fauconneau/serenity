#include "widget.h"
#include "gl.h"
#include "matrix.h"
#include "simd.h"
FILE(shader_glsl)

struct State {
 float bottomZ = 0, topZ = 0;
 struct Grain {
  float radius;
  size_t count;
  ref<float> Px;
  ref<float> Py;
  ref<float> Pz;
  ref<float> Vx;
  ref<float> Vy;
  ref<float> Vz;
  ref<float> Fx;
  ref<float> Fy;
  ref<float> Fz;
  ref<float> Rx;
  ref<float> Ry;
  ref<float> Rz;
  ref<float> Rw;
  ref<float> AVx;
  ref<float> AVy;
  ref<float> AVz;
  ref<float> Tx;
  ref<float> Ty;
  ref<float> Tz;
  vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  //vec4 rotation(size_t i) const { return vec4(Rx[i], Ry[i], Rz[i], Rw[i]); }
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
  size_t count;
  float radius;
  ref<float> Px;
  ref<float> Py;
  ref<float> Pz;
  ref<float> Vx;
  ref<float> Vy;
  ref<float> Vz;
  ref<float> Fx;
  ref<float> Fy;
  ref<float> Fz;
  vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]); }
  vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]); }
 } membrane;
 struct Force { vec3 origin, force; };
 ref<Force> forces;
};

struct StateView : Widget {
 State state;
 size_t timeStep = 0;

 vec2 yawPitch = vec2(0, -PI/2); // Current view angles

 struct {
  vec2 cursor;
  vec2 yawPitch;
  size_t timeStep;
 } dragStart = {0,0,0};

 array<int> grainIndices;
 GLIndexBuffer indexBuffer;
 GLBuffer xBuffer, yBuffer, zBuffer;

 vec2 sizeHint(vec2) override { return vec2(992); }

 shared<Graphics> graphics(vec2 size) override {
  state.bottomZ = 0, state.topZ = 0;
  for(float z: state.grain.Pz) {
   state.bottomZ = min(state.bottomZ, z);
   state.topZ = max(state.topZ, z);
  }

  vec4 viewRotation = qmul(angleVector(yawPitch.y, vec3(1,0,0)), angleVector(yawPitch.x, vec3(0,0,1)));
  const vec3 scale = vec3(vec2(2/(state.topZ-state.bottomZ)), 1./16);
  mat4 viewProjection = mat4()
    .scale(vec3(1,1,-1))
    .scale(scale)
    .rotateX(yawPitch.y).rotateZ(yawPitch.x)
    .translate(vec3(0,0,-(state.bottomZ+state.topZ)/2));

  if(state.grain.count) {
   for(int i: range(grainIndices.size, state.grain.count)) grainIndices.append(i);
   buffer<float> grainZ (align(simd, grainIndices.size));
   {
    const vXsf Qx = floatX(viewRotation.x);
    const vXsf Qy = floatX(viewRotation.y);
    const vXsf Qz = floatX(viewRotation.z);
    const vXsf Qw = floatX(viewRotation.w);
    const float* pPx = state.grain.Px.data, *pPy = state.grain.Py.data, *pPz = state.grain.Pz.data;
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

   glDepthTest(true);

   buffer<vec3> positions {grainIndices.size*6};
   buffer<vec4> R {grainIndices.size};
   for(size_t i: range(grainIndices.size)) {
    // FIXME: GPU quad projection
    vec3 O = viewProjection * state.grain.position(grainIndices[i]);
    vec2 min = O.xy() - vec2(scale.xy()) * vec2(state.grain.radius) - vec2(2.f/size.x); // Parallel
    vec2 max = O.xy() + vec2(scale.xy()) * vec2(state.grain.radius) + vec2(2.f/size.x); // Parallel
    positions[i*6+0] = vec3(min, O.z);
    positions[i*6+1] = vec3(max.x, min.y, O.z);
    positions[i*6+2] = vec3(min.x, max.y, O.z);
    positions[i*6+3] = vec3(min.x, max.y, O.z);
    positions[i*6+4] = vec3(max.x, min.y, O.z);
    positions[i*6+5] = vec3(max, O.z);
    R[i] = vec4(state.grain.Rx[grainIndices[i]],
                      state.grain.Ry[grainIndices[i]],
                      state.grain.Rz[grainIndices[i]],
                      state.grain.Rw[grainIndices[i]]);
   }

   static GLShader shader {::shader_glsl(), {"interleaved sphere"}};
   shader.bind();
   shader["transform"] = mat4(1);
   shader.bindFragments({"color"});
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   GLBuffer RBuffer (R);
   shader.bind("R"_, RBuffer);
   shader["viewRotation"] = viewRotation;
   /*GLBuffer colorBuffer (colors);
   shader.bind("colorBuffer"_, colorBuffer, 1);*/
   // Reduces Z radius to see membrane mesh on/in grain
   shader["radius"] = float(scale.z/2 * state.grain.radius * 7/8);
   shader["hpxRadius"] = 1 / (size.x * scale.x * state.grain.radius);
   vertexArray.draw(Triangles, positions.size);
  }

#if WIRE
  size_t wireCount = state.wire.count;
  if(wireCount>1) {
   buffer<vec3> positions {size_t(wireCount-1)*6};
   buffer<vec4> colors {size_t(wireCount-1)};
   size_t s = 0;
   for(int i: range(wireCount-1)) {
    vec3 a (state.wire.position(i)), b (state.wire.position(i+1));
    // FIXME: GPU quad projection
    vec3 A = viewProjection * a, B= viewProjection * b;
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
    colors[s] = cylinders.contains(i) ? vec4(1,0,0,1) : vec4(1, 1, 1, 1);
    s++;
   }
   assert(s*6 <= positions.size);
   positions.size = s*6;
   if(positions.size) {
    static GLShader shader {::shader_glsl(), {"interleaved cylinder"}};
    shader.bind();
    shader.bindFragments({"color"});
    shader["transform"] = mat4(1);
    shader["radius"] = float(scale.z/2 * Wire::radius);
    shader["hpxRadius"] = 1 / (size.x * scale.x * Wire::radius);
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

  float maxF = 0;
  for(size_t i: range(state.membrane.count))
   maxF = ::max(maxF, length(state.membrane.force(i)));
  for(size_t i: range(state.forces.size))
   if(isNumber(length(state.forces[i].force)))
    maxF = ::max(maxF, length(state.forces[i].force));
  assert_(isNumber(maxF));

  if(state.membrane.count) {
   if(!indexBuffer) {
    const int W = state.membrane.W, stride = state.membrane.stride,
      margin = state.membrane.margin;
    buffer<uint16> indices(W*(state.membrane.H-1)*6-W*2);
    int s = 0;
    for(int i: range(0, state.membrane.H-1)) {
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
   if(!xBuffer) xBuffer = GLBuffer(state.membrane.Px);
   xBuffer.upload(state.membrane.Px);
   vertexArray.bindAttribute(shader.attribLocation("x"_), 1, Float, xBuffer);
   if(!yBuffer) yBuffer = GLBuffer(state.membrane.Py);
   yBuffer.upload(state.membrane.Py);
   vertexArray.bindAttribute(shader.attribLocation("y"_), 1, Float, yBuffer);
   if(!zBuffer) zBuffer = GLBuffer(state.membrane.Pz);
   zBuffer.upload(state.membrane.Pz);
   vertexArray.bindAttribute(shader.attribLocation("z"_), 1, Float, zBuffer);
   vertexArray.bind();
   indexBuffer.draw();

   // Membrane force sum
   if(1) {
    array<vec3> positions;
    float scale = state.membrane.radius/maxF;
    for(size_t i: range(state.membrane.count)) {
     positions.append(state.membrane.position(i));
     positions.append(state.membrane.position(i) + scale*state.membrane.force(i));
    }
    if(positions) {
     static GLShader shader {::shader_glsl(), {"interleaved flat"}};
     shader.bind();
     shader.bindFragments({"color"});
     shader["transform"] = viewProjection;
     shader["uColor"] = vec4(0,0,1, 1/*./2*/);
     static GLVertexArray vertexArray;
     GLBuffer positionBuffer (positions);
     vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
     extern float lineWidth; lineWidth = 1/*./2*/;
     glBlendAlpha();
     vertexArray.draw(Lines, positions.size);
    }
   }
  }

  // Forces
  {
   array<vec3> positions;
   float scale = state.membrane.radius/maxF;
   for(size_t i: range(state.forces.size)) {
    if(isNumber(state.forces[i].force)) {
     positions.append(state.forces[i].origin);
     positions.append(state.forces[i].origin - scale*state.forces[i].force);
    }
   }
   if(positions) {
    static GLShader shader {::shader_glsl(), {"interleaved flat"}};
    shader.bind();
    shader.bindFragments({"color"});
    shader["transform"] = viewProjection;
    shader["uColor"] = vec4(1,0,0, 1/*./2*/);
    static GLVertexArray vertexArray;
    GLBuffer positionBuffer (positions);
    vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
    extern float lineWidth; lineWidth = 1/*./2*/;
    glBlendAlpha();
    vertexArray.draw(Lines, positions.size);
   }
  }

#if 0
  // Lines
  if(0) {
   static GLShader shader {::shader_glsl(), {"interleaved flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["uColor"] = vec4(black, 1);

   // Plates
   size_t N = 64;
   array<vec3> positions; //buffer<vec3> positions {N*2*2, 0};
   const int radius = state.membrane.radius;
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (radius*cos(a1), radius*sin(a1), state.bottomZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (radius*cos(a2), radius*sin(a2), state.bottomZ);
    vec3 A = viewProjection * a, B= viewProjection * b;
    positions.append(A); positions.append(B);
   }
   for(size_t i: range(N)) {
    float a1 = 2*PI*(i+0)/N; vec3 a (radius*cos(a1), radius*sin(a1), state.topZ);
    float a2 = 2*PI*(i+1)/N; vec3 b (radius*cos(a2), radius*sin(a2), state.topZ);
    vec3 A = viewProjection * a, B= viewProjection * b;
    positions.append(A); positions.append(B);
   }
   {Locker lock(::lock);
   for(vec2x3 l: ::lines) { positions.append(viewProjection * l.a); positions.append(viewProjection * l.b); }}
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Lines, positions.size);
  }

  if(::faces && 0) {
   static GLShader shader {::shader_glsl(), {"interleaved flat"}};
   shader.bind();
   shader.bindFragments({"color"});
   shader["transform"] = mat4(1);
   shader["uColor"] = vec4(1,0,0, 1);
   array<vec3> positions;
   const int margin = state.membrane.margin;
   const int stride = state.membrane.stride;
   {Locker lock(::lock);
    for(int v: ::faces) {
     int b = v/2, I = v%2;
     int rowIndex = (b-margin)/stride;
     int e0, e1;
     // FIXME: assert peeled
     if(I == 0) { // (., 0, 1)
      e0 = rowIndex%2 -stride;
      e1 = rowIndex%2 -stride-1;
     } else { // (., 1, 2)
      e0 = rowIndex%2 -stride-1;
      e1 = -1;
     }
     positions.append(viewProjection * vec3(state.membrane.position(b)));
     positions.append(viewProjection * vec3(state.membrane.position(b+e0)));
     positions.append(viewProjection * vec3(state.membrane.position(b+e1)));
    }
   }
   static GLVertexArray vertexArray;
   GLBuffer positionBuffer (positions);
   vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
   vertexArray.draw(Triangles, positions.size);
  }
#endif

  return shared<Graphics>();
 }

 // Orbital ("turntable") view control
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
  if(event == Press) dragStart = {cursor, yawPitch, timeStep};
  if(event==Motion && button==LeftButton) {
   yawPitch = dragStart.yawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
   yawPitch.y = clamp<float>(-PI, yawPitch.y, 0);
  }
  else return false;
  return true;
 }
};
