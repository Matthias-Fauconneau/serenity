#include "window.h"
#include "render.h"
#include "png.h"
#include "pdf.h"

struct Pattern : Widget, Poll {
 unique<Window> window = ::window(this);
 const int type = 2;
 const float loopAngle = PI*(3-sqrt(5.)) / (type == 1 ? 1 : 2);
 const float dt = 1e-3;
 const float winchRate = 1;
 const float winchRadius = 1;
 float lastAngle = 0, winchAngle = 0, currentRadius = winchRadius;
 array<vec2> positions;
 Pattern() {
  while(winchAngle < 5*2*PI) step();
  {array<char> s;
   for(size_t i: range(0, positions.size-1)) {
    auto a = positions[i], b = positions[i+1];
    s.append(str(a[0], a[1], b[0], b[1])+'\n');
   }
   writeFile("pattern", s, currentWorkingDirectory(), true);
  }
  int2 size(1050);
  auto graphics = this->graphics(vec2(size));
  string name = "pattern";
  //writeFile(name+".pdf", toPDF(72, ref<Graphics>(graphics.pointer,1)), currentWorkingDirectory(), true);
  Image target = render(size, graphics);
  writeFile(name+".png", encodePNG(target), currentWorkingDirectory(), true);
  //queue();
 }
 void event() { step(); window->render(); queue(); }
 void step() {
  vec2 p;
  if(type==0) { // Simple helix
   float a= winchAngle;
   float r = winchRadius;
   p = vec2(r*cos(a), r*sin(a));
   winchAngle += winchRate * dt;
  }
  else if(type==1) { // Cross reinforced helix
   if(currentRadius < -winchRadius) { // Radial -> Tangential (Phase reset)
    currentRadius = winchRadius;
    lastAngle = winchAngle+PI;
    winchAngle = lastAngle;
   }
   if(winchAngle < lastAngle+loopAngle) { // Tangential phase
    float a = winchAngle;
    float r = winchRadius;
    p = vec2(r*cos(a), r*sin(a));
    winchAngle += winchRate * dt;
   } else { // Radial phase
    float a = winchAngle;
    float r = currentRadius;
    p = vec2(r*cos(a), r*sin(a));
    currentRadius -= winchRate*2*PI*winchRadius * dt; // Constant velocity
   }
  }
  else { // Loops
   float A = winchAngle, a = winchAngle * loopAngle / (2*PI);
   float R = winchRadius, r = 1./2*R;
   p = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
   winchAngle += winchRate * dt;
  }
  positions.append(p);
 }
 vec2 sizeHint(vec2) override { return 1050; }
 shared<Graphics> graphics(vec2 size) override {
  float scale = min(size.x, size.y)/2;
  vec2 offset = (size-vec2(2*scale))/2.f + vec2(scale);
  shared<Graphics> graphics;
  for(size_t i: range(positions.size-1))
   graphics->lines.append(offset + scale*positions[i], offset + scale*positions[i+1]);
  return graphics;
 }
} app;
