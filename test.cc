#include "window.h"
#include "jpeg.h"
#include "matrix.h"

typedef ImageT<bgr3f> Image3f;

inline void blend(const Image3f& target, int x, int y, bgr3f color, float opacity=1) {
 /*if(opacity==1) target(x, y) = color;
 else*/ target(x, y) = target(x, y)*(1-opacity) + opacity*color;
}

void blend(const Image3f& target, int x, int y, int z, bgr3f color, float opacity=1) {
 const int X = target.size.x/2+x+y, Y=target.size.y/2+x-y-2*z;
 if(Y<0) return;
 if(X%2) {
  blend(target, X-1, Y+0, color, opacity);
  blend(target, X,    Y+1, color, opacity);
  blend(target, X-1, Y+1, color, opacity);
  blend(target, X,    Y+2, color, opacity);
  blend(target, X-1, Y+2, color, opacity);
  blend(target, X,    Y+3, color, opacity);
 } else {
  blend(target, X,    Y+0, color, opacity);
  blend(target, X-1, Y+1, color, opacity);
  blend(target, X,    Y+1, color, opacity);
  blend(target, X-1, Y+2, color, opacity);
  blend(target, X,    Y+2, color, opacity);
  blend(target, X-1, Y+3, color, opacity);
 }
}

/// 3D array of voxels
struct Voxel {
 vec3 normal;
 float opacity = 0;
 bgr3f color;
};
struct Grid : buffer<Voxel> {
 int3 size = 0;
 Grid() {}
 Grid(int3 size) : buffer<Voxel>(size.x*size.y*size.z), size(size) { clear(); }
 inline Voxel& operator()(uint x, uint y, uint z) const { return buffer<Voxel>::at(z*size.y*size.x+y*size.x+x); }
};

struct Test : Widget {
 Random random;
#if 0
 Grid grid {int3(640,640,16)};
 void next() {
  for(int unused i: range(grid.size.x*grid.size.y)) {
   int R = 5;
   int x = R+random%(grid.size.x-2*R), y = R+random%(grid.size.y-2*R);
   int N = 0;
   for(int dy: range(-R, R+1)) for(int dx: range(-R, R+1)) if((dx || dy) && grid(x+dx, y+dy, 0)) N++;
   if(N) continue;
   int z = 0;
   while(z < grid.size.z && grid(x,y,z)) z++;
   if(z < grid.size.z) grid(x,y,z) = float(1+z)/grid.size.z;
  }
 }
#else
 Grid grid {int3(256)};
 struct Node {
  vec3 axis;
  float length;
  float baseRadius, endRadius;
  int order;
  float axisLength;
  bool bud;
  vec3 nextAxis;
  array<unique<Node>> branches;
  Node(vec3 axis, float length, float baseRadius, float endRadius, int order, float axisLength=0)
   : axis(axis), length(length), baseRadius(baseRadius), endRadius(endRadius), order(order), axisLength(axisLength), bud(true), nextAxis(axis) {}

  void grow(Random& random, int age, vec3 origin=vec3(0)) {
   // Tropism
   axis = normalize(axis+vec3(0,0,1./(8*(order+1))/*+length/meter*/)); // Weight
   // Grows
   const float growthRate = 1+1./(16*(age+1)*(order+1));
   length *= growthRate;
   baseRadius *= growthRate;
   vec3 end = origin+length*axis;
   for(auto& branch: branches) branch->grow(random, age, end);
   if(order==3 /*|| (order>=1 && axisLength>1)*/) bud = false;
   if(bud) { // Shoots new branches
    // Coordinate system
    vec3 z = axis;
    vec3 x = cross(z, vec3(0,1,0)); x = normalize(::length(x)?x:cross(z,vec3(0,0,1)));
    vec3 y = normalize(cross(z, x));
    {// Main axis
     const float gnarl = order==0 ? 1./64 : 1./32;
     float xAngle = 2*PI*(random()*2-1) * gnarl;
     float yAngle = 2*PI*(random()*2-1) * gnarl;
     vec3 main = normalize(sin(xAngle)*x + sin(yAngle)*y + cos(xAngle)*cos(yAngle)*z);
     branches.append(main, length*(1-1./4), endRadius, endRadius*(1-1./4), order, axisLength+length);
    }
    if(order==0 && age<6) {// -- Whorl
     const int branchCount = order==0 ? 7+random%(11-7) :  2+random%(5-2);
     float phase = 2*PI*random();
     for(int i: range(branchCount)) {
      float angle = phase + 2*PI*i/branchCount + (2*PI*random() / branchCount);
      float zAngle = PI / 4 + (2*PI*random() / 16);
      float length = this->length/2;
      vec3 shootAxis = sin(zAngle)*(cos(angle)*x + sin(angle)*y) + cos(zAngle)*z;
      //shootAxis = normalize(shootAxis-vec3(0,0,1+length)); // Weight
      //branches.append(shootAxis, endRadius+length, endRadius/2 + min(baseRadius, 1.f/10), (endRadius*(1-1./4))/2 + min(baseRadius, 1.f/10), order+1);
      branches.append(shootAxis, /*endRadius+*/length, endRadius/2, (endRadius*(1-1./4))/2, order+1);
     }
    }
   }
   bud = false;
   nextAxis = branches ? branches[0]->axis : axis; // First is main axis
   endRadius = branches ?  branches[0]->baseRadius : min(baseRadius/2, 1.f/10) ;
  }
 };

 mat3 rotate;
 void geometry(const Node& node, vec3 origin=vec3(0)) {
  const bgr3f trunkColor (0., 3./12, 5./12);
  const bgr3f branchColor (0, 6./12., 4./12); //vec3(1./2, 1./3, 0);
  vec3 end = origin+node.length*rotate*node.axis;
  internode(origin, end, node.baseRadius, node.endRadius, node.order ? branchColor : trunkColor); //, node.axis, node.nextAxis,
  if(node.order>0) leaves(origin, end, node.baseRadius*8, node.endRadius*8);
  if(node.branches) for(auto& branch: node.branches) geometry(branch, end);
 }

 //int age = 0;
 Node root {vec3(0,0,1), 64, 4, 4, 0, 0}; // Trunk axis root

 void internode(vec3 A, vec3 B, float rA, float rB, bgr3f color) {//, vec3 zA, vec3 zB
  int3 min = ::max(int3(0), int3(floor(::min(A-vec3(rA), B-vec3(rB))))), max = ::min(grid.size-int3(1), int3(ceil(::max(A+vec3(rA), B+vec3(rB)))));
  vec3 O = A;
  vec3 D = B-A;
  float L = length(D);
  vec3 N = D/L;
  for(int z: range(min.z, max.z)) for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
   vec3 P (x,y,z);
   float t = dot(N, P-O)/L;
   if(t >= 0 && t <= 1) {
    float R = rA+t*(rB-rA);
    vec3 o = A + t*D;
    vec3 n = P-o;
    float r = length(n);
    if(r <= R) grid(x,y,z) = {n/r,1,color};
   }
  }
 }
 void leaves(vec3 A, vec3 B, float rA, float rB) {//, vec3 zA, vec3 zB, vec3 color) {
  int3 min = ::max(int3(0), int3(floor(::min(A-vec3(rA), B-vec3(rB))))), max = ::min(grid.size-int3(1), int3(ceil(::max(A+vec3(rA), B+vec3(rB)))));
  vec3 O = A;
  vec3 D = B-A;
  float L = length(D);
  vec3 N = D/L;
  float V = PI/3*(sq(rA)+rA*rB+sq(rB))*L;
  for(int v=0; v<V/8;) {
   int x = min.x+random%(max.x-min.x);
   int y = min.y+random%(max.y-min.y);
   int z = min.z+random%(max.z-min.z);
   if(grid(x,y,z).opacity) continue;
   vec3 P (x,y,z);
   float t = dot(N, P-O)/L;
   if(t >= 0 && t <= 5./4) {
    float R = rA+t*(rB-rA);
    vec3 o = A + t*D;
    vec3 n = P-o;
    float r = length(n);
    if(r <= R) {
     float xAngle = PI/2*(random()*2-1);
     float yAngle = PI/2*(random()*2-1);
     vec3 N = normalize(sin(xAngle)*vec3(1,0,0) + sin(yAngle)*vec3(0,1,0) + cos(xAngle)*cos(yAngle)*vec3(0,0,1));
     grid(x,y,z) = {N, 1/4.f, bgr3f(0,1,0)};
     v++;
    }
   }
  }
 }

 /*void next() {
  grid.clear(0);
  log(age);
  root.grow(random, age);
  age++;
  geometry(root, vec3(grid.size.x/2, grid.size.y/2, 0));
 }*/
 float angle = 0;
 void next() {
   grid.clear();
   rotate = mat3().rotateZ(angle);
   angle += 2*PI/60;
   geometry(root, vec3(grid.size.x/2, grid.size.y/2, 0));
 }
#endif

 unique<Window> window = ::window(this);
 Test() {
  window->backgroundColor = nan;
  //window->actions[Space] = [this]{ next(); window->render(); };
  window->presentComplete = [this]{ next(); window->render(); };
  //grid(grid.size.x/2, grid.size.y/2, 0) = 1;
  for(int age: range(4)) root.grow(random, age);
  geometry(root, vec3(grid.size.x/2, grid.size.y/2, 0));
 }
 vec2 sizeHint(vec2) override { return vec2(2*640, 640); }
 shared<Graphics> graphics(vec2) override {
  const Image& target = window->target;
  int2 size = target.size;
  Image3f sub (size.x, size.y*2);
  sub.clear(0);
#if 0
  const int M = size.x/2;
  ImageF heightmap(M+1, M+1);
  heightmap.clear(0);
  Image test = resize(int2(M+1), decodeImage(Map("test.jpg")));
  for(int X: range(0, M+1)) for(int Y: reverse_range(M -2/*FIXME*/, 2/*-1*/)) {
   heightmap(X, Y) = float(test(X,Y).g)*M/64 /0xFF;
  }
  for(int X: range(1, M)) for(int Y: reverse_range(M-1, 0)) {
   float Z = heightmap(X, Y);
   float dxZ = heightmap(X+1, Y) - heightmap(X-1, Y);
   float dyZ = heightmap(X, Y+1) - heightmap(X, Y-1);
   float l = sqrt(1+sq(dxZ)+sq(dyZ));
   vec3 N (-dxZ/l, -dyZ/l, 1/l);
   vec3 L = normalize(vec3(0,1,1));
   float NdotL = max(0.f, dot(N, L));
   if(Z <= 0)
    blend(M/2+X,M/2+Y,0,byte3(0xFF,0,0));
   else
    blend(M/2+X,M/2+Y,Z,byte3(0xFF*(1+NdotL)/2*bgr3f(0,1,1)));
  }
#else
  for(int X: range(grid.size.x)) for(int Y: reverse_range(grid.size.y)) for(int Z: range(grid.size.z)) {
   Voxel v = grid(X, Y, Z);
   if(!v.opacity) continue;
   vec3 L = normalize(vec3(0,1,1));
   //float NdotL = max(0.f, dot(N, L)); // Direct
   float NdotL = (1+dot(v.normal, L))/2; // Hemispheric
   blend(sub, X-grid.size.x/2, Y-grid.size.y/2, Z, NdotL*v.color, v.opacity);
  }
#endif
  for(int y: range(size.y)) for(int x: range(size.x)) target(x, y) = byte3((sub(x, y*2+0)+sub(x, y*2+1))*(0xFF/2.f));
  //upsample(target, upsample(cropRef(target, target.size/2-target.size/8, target.size/4)));
  return shared<Graphics>();
 }
} test;
