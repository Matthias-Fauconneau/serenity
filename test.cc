#include "window.h"

struct Test : Widget {
 unique<Window> window = nullptr;
 Test() {
  window = ::window(this);
  window->backgroundColor = nan;
  //window->presentComplete = [this]{ next(); window->render(); };
 }
 vec2 sizeHint(vec2) override { return vec2(512); }
 shared<Graphics> graphics(vec2) override {
  const Image& target = window->target;
  target.clear(byte4(0,0,0,0xFF));
  int2 size = window->target.size;
  const int P = 32;
  int2 N = size/P;
  for(int y: range(N.y)) {
   for(int x: range(N.x)) {
    {for(int dx: range(-1, 0+1)) for(int dy: range(-P/4, P/4+1)) target(x*P+P/2+dx, y*P+P/2+dy).g = 0x80; }
    {for(int dy: range(-1, 0+1)) for(int dx: range(-P/4, P/4+1)) target(x*P+P/2+dx, y*P+P/2+dy).g = 0x80; }
   }
  }
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
  blend(0,0,0,byte3(0x40,0x40,0x40));
  blend(0,1,0,byte3(0,0xFF,0));
  blend(1,1,0,byte3(0,0xFF,0xFF));
  blend(1,0,0,byte3(0,0,0xFF));
  blend(0,1,1,byte3(0xFF,0xFF,0));
  blend(1,1,1,byte3(0xFF,0xFF,0xFF));
  blend(0,0,1,byte3(0xFF,0,0));
  blend(1,0,1,byte3(0xFF,0,0xFF));

  for(int Y: range(N.y+1)) {
   for(int X: range(N.x+1)) {
    for(int dy: range(-P, 0)) {
     for(int dx: range(-P, 0)) {
      int y = Y*P+P/2+dy-(X%2?P/2:0), x = X*P+P/2+dx;
      if(2*dy+dx >= -2*P && 2*dy-dx < 0 && y>=0 && y<target.size.y && x>=0 && x<target.size.x) {
       //target(x, y).b = 0xFF;
       target(x, y) = sub(X, Y*2+0);
      }
     }
    }
    for(int dy: range(-P/2, P/2+1)) {
     for(int dx: range(-P, 0)) {
      int y = Y*P+P/2+dy-(X%2?P/2:0), x = X*P+P/2+dx;
      if(2*dy-dx >= 0 && 2*dy+dx < 0 && y>=0 && y<target.size.y && x>=0 && x<target.size.x) {
       //target(x, y).r = 0xFF;
       target(x, y) = sub(X, Y*2+1);
      }
     }
    }
   }
  }
  //for(byte4 v: target) assert_(v!=byte4(0,0,0,0xFF) && (v.r ^ v.b));
  return shared<Graphics>();
 }
} test;
