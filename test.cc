#include "window.h"
#include "jpeg.h"

struct Test : Widget {
 unique<Window> window = nullptr;
 Test() {
  window = ::window(this);
  window->backgroundColor = nan;
  //window->presentComplete = [this]{ next(); window->render(); };
 }
 vec2 sizeHint(vec2) override { return vec2(2*640, 640); }
 shared<Graphics> graphics(vec2) override {
  const Image& target = window->target;
  //Image target(window->target.size);
  target.clear(byte4(0,0,0,0xFF));
  int2 size = target.size;
  const int P = 1;
  int2 N = size/P;
  /*for(int y: range(N.y)) {
   for(int x: range(N.x)) {
    {for(int dx: range(-1, 0+1)) for(int dy: range(-P/4, P/4+1)) target(x*P+P/2+dx, y*P+P/2+dy).g = 0x80; }
    {for(int dy: range(-1, 0+1)) for(int dx: range(-P/4, P/4+1)) target(x*P+P/2+dx, y*P+P/2+dy).g = 0x80; }
   }
  }*/
  Image sub (N.x+1, (N.y+1)*2);
  sub.clear(byte4(0,0,0,0xFF));
  auto blend = [&sub](int x, int y, int z, byte3 color) {
   const int X = sub.size.x/2+x+y, Y=sub.size.y/2+x-y-2*z;
   if(!(X%2)) {
    sub(X, Y).bgr() = color;
    sub(X-1, Y+1).bgr() = color;
    sub(X, Y+1).bgr() = color;
    sub(X-1, Y+2).bgr() = color;
    sub(X, Y+2).bgr() = color;
    sub(X-1, Y+3).bgr() = color;
   } else {
    sub(X-1, Y).bgr() = color;
    sub(X-1, Y+1).bgr() = color;
    sub(X, Y+1).bgr() = color;
    sub(X-1, Y+2).bgr() = color;
    sub(X, Y+2).bgr() = color;
    sub(X, Y+3).bgr() = color;
   }
  };
  /*blend(0,1,0,byte3(0,0xFF,0));
  blend(1,1,0,byte3(0,0xFF,0xFF));
  blend(0,0,0,byte3(0x40,0x40,0x40));
  blend(1,0,0,byte3(0,0,0xFF));*/
  /*blend(0,1,1,byte3(0xFF,0xFF,0));
  blend(1,1,1,byte3(0xFF,0xFF,0xFF));
  blend(0,0,1,byte3(0xFF,0,0));
  blend(1,0,1,byte3(0xFF,0,0xFF));*/
  {
   const int M = size.x/P/4;
   ImageF heightmap(2*M+1, 2*M+1);
   heightmap.clear(0);
   Image test = resize(int2(2*M+1), decodeImage(Map("test.jpg")));
   for(int X: range(0, 2*M+1)) for(int Y: reverse_range(2*M -2/*FIXME*/, 2/*-1*/)) {
    //heightmap(X, Y) = sin(PI*(X-M)/float(M))*sin(PI*(Y-M)/float(M)) *M/4;
    heightmap(X, Y) = float(test(X,Y).g)*M/32 /0xFF;
   }
   for(int X: range(1, 2*M)) for(int Y: reverse_range(2*M-1, 0)) {
    float Z = heightmap(X, Y);
    float dxZ = heightmap(X+1, Y) - heightmap(X-1, Y);
    float dyZ = heightmap(X, Y+1) - heightmap(X, Y-1);
    float l = sqrt(1+sq(dxZ)+sq(dyZ));
    vec3 N (-dxZ/l, -dyZ/l, 1/l);
    vec3 L (-0/sqrt(2.f),1/sqrt(2.f),1/sqrt(2.f));
    float NdotL = max(0.f, dot(N, L));
    //blend(X,Y,Z*N/4,byte3(0x80+0x7F*(1+Z)/2));
    //blend(X,Y,0,byte3(0xFF));
    //blend(X,Y,0,byte3(X%2 ? 0xFF : 0));
    //blend(X,Y,0,byte3(((abs(X)%2)^(abs(Y)%2)) ? 0xFF : 0));
    if(Z <= 0)
     blend(M+X,M+Y,0,byte3(0xFF,0,0));
    else
     blend(M+X,M+Y,Z,byte3(0xFF*(1+NdotL)/2*bgr3f(0,1,1)));
   }
  }

  if(P==1) {
   for(int y: range(N.y-1)) {
    for(int x: range(N.x)) {
     target(x, y) = byte4((int4(sub(x, y*2+0))+int4(sub(x, y*2+1)))/2);
    }
   }
  } else {
   for(int Y: range(N.y+1)) {
    for(int X: range(N.x+1)) {
     for(int dy: range(-P, 0)) {
      for(int dx: range(-P, 0)) {
       int y = Y*P+P/2+dy-(X%2?P/2:0), x = X*P+P/2+dx;
       if(2*dy+dx >= -2*P && 2*dy-dx <= 0 && y>=0 && y<target.size.y && x>=0 && x<target.size.x) {
        target(x, y) = sub(X, Y*2+0);
        //target(x, y).b = 0xFF;
       }
      }
     }
     for(int dy: range(-P/2, P/2+1)) {
      for(int dx: range(-P, 0)) {
       int y = Y*P+P/2+dy-(X%2?P/2:0), x = X*P+P/2+dx;
       if(2*dy-dx > 0 && 2*dy+dx < 0 && y>=0 && y<target.size.y && x>=0 && x<target.size.x) {
        target(x, y) = sub(X, Y*2+1);
        //target(x, y).r = 0xFF;
       }
      }
     }
    }
   }
  }
  //for(byte4 v: target) assert_(v!=byte4(0,0,0,0xFF) && (v.r ^ v.b));
  //upsample(window->target, upsample(target));
  return shared<Graphics>();
 }
} test;
