#include "keyboard.h"
#include "render.h"

#if 1
void Keyboard::render(const Image& target) {
 int y1 = target.size.y*2/3;
 int y2 = target.size.y;
 int dx = round(target.size.x/88.f);
 int margin = (target.size.x-88*dx)/2;
 // Top side

 for(uint key=0; key<88; key++) {
  int x0 = margin + key*dx;
  int x1 = x0 + dx;
  fill(target, int2(x0, 0), int2(1, y1-1), black);

  int notch[12] = { 3, 1, 4, 0, 1, 2, 1, 4, 0, 1, 2, 1 };
  int l = notch[key%12], r = notch[(key+1)%12];
  if(key==0) l=0; //A-1 has no left notch
  bgr3f color = left.contains(key+21) ? red : right.contains(key+21) ? green : unknown.contains(key+21) ? blue : (l==1 ? black : white);
  if(l==1) { // black key
   fill(target, int2(x0, y1-1), int2(x1-x0, 1), black);
   fill(target, int2(x0+1,0), int2(dx+1,y1-1), color);
  } else {
   fill(target, int2(x0+1, 0), int2(dx, y2), color); // white key
   fill(target, int2(x0-l*dx/6, y1-1), int2(1, y2-(y1-1)));
   {int x = x0+1-l*dx/6; fill(target, int2(x, y1), int2(x1-x, y2-y1), color);} //left notch
   if(key!=87) fill(target, int2(x1,y1), int2((6-r)*dx/6 -1, y2-y1), color); //right notch
   //right edge will be next left edge
  }
  if(key==87) { //C7 has no right notch
   fill(target, int2(x1+dx/2, 0), int2(1, y2), color);
   fill(target, int2(x1, 0), int2(x1+dx/2, y1-1), color);
  }
 }
}
#else
shared<Graphics> Keyboard::graphics(vec2 size) {
 shared<Graphics> graphics;
 int y1 = size.y*2/3;
 int y2 = size.y;
 int dx = round(size.x/88.f);
 int margin = (size.x-88*dx)/2;
 for(uint key=0; key<88; key++) {
  int x0 = margin + key*dx;
  int x1 = x0 + dx;
  //graphics->lines.append(vec2(x0, 0), vec2(x0, y1-1));
  graphics->fills.append(vec2(x0, 0), vec2(1, y1-1));

  int notch[12] = { 3, 1, 4, 0, 1, 2, 1, 4, 0, 1, 2, 1 };
  int l = notch[key%12], r = notch[(key+1)%12];
  if(key==0) l=0; //A-1 has no left notch
  bgr3f color = left.contains(key+21) ? red : right.contains(key+21) ? green : unknown.contains(key+21) ? blue : (l==1 ? black : white);
  if(l==1) { // black key
   //graphics->lines.append(vec2(x0, y1-1), vec2(x1, y1-1));
   graphics->fills.append(vec2(x0, y1-1), vec2(x1-x0, 1));
   graphics->fills.append(vec2(x0+1,0), vec2(dx+1,y1-1), color);
  } else {
   graphics->fills.append(vec2(x0+1, 0), vec2(dx, y2), color); // white key
   //graphics->lines.append(vec2(x0-l*dx/6, y1-1), vec2(x0-l*dx/6, y2)); //left edge
   graphics->fills.append(vec2(x0-l*dx/6, y1-1), vec2(1, y2-(y1-1)));
   {int x = x0+1-l*dx/6; graphics->fills.append(vec2(x, y1), vec2(x1-x, y2-y1), color);} //left notch
   if(key!=87) graphics->fills.append(vec2(x1,y1), vec2((6-r)*dx/6 -1, y2-y1), color); //right notch
   //right edge will be next left edge
  }
  if(key==87) { //C7 has no right notch
   //graphics->lines.append(vec2(x1+dx/2, 0), vec2(x1+dx/2, y2));
   graphics->fills.append(vec2(x1+dx/2, 0), vec2(1, y2), color);
   graphics->fills.append(vec2(x1, 0), vec2(x1+dx/2, y1-1), color);
  }
 }
 return graphics;
}
#endif
