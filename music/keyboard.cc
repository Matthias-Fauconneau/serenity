#include "keyboard.h"
#include "render.h"

void gradientY(const Image& target, int x0, int dx, int y0, int dy, bgr3f color0, bgr3f color1, float opacity=1, Op op=Src) {
 for(int y: range(dy)) {
  const float t = float(y)/(dy-1);
  fill(target, int2(x0, y0+y), int2(dx, 1), color0 + t*(color1-color0), opacity, op);
 }
}

void gradientX(const Image& target, int x0, int dx, int y0, int dy, bgr3f color0, bgr3f color1, float opacity=1, Op op=Src) {
 for(int x: range(dx)) {
  const float t = float(x)/(dx-1);
  fill(target, int2(x0+x, y0), int2(1, dy), color0 + t*(color1-color0), opacity, op);
 }
}

void gradientR(const Image& target, int x0, int dx, int y0, int dy, bgr3f color0, bgr3f color1, float opacity=1, Op op=Src) {
  for(int y: range(dy)) for(int x: range(dx)) {
   const float t = sqrt(float(sq(x)+sq(y))/float(sq(dx-1)));
   fill(target, int2(x0+x, y0+y), int2(1, 1), color0 + t*(color1-color0), opacity, op);
  }
}

#if 1
void Keyboard::render(const Image& target) {
 const int y0b = 6;
 const int y0w = y0b+3;
 const int y1w = target.size.y-1;
 const int y1wr = y1w-y1w/12;
 const int y1b = y1w*2/3;
 const int y1br = y1b-y1b/12;
 const int X = target.size.x;
 const int dx = round(X/88.f);
 const int margin = (X-88*dx)/2;
 gradientY(target, 0, X, 0, y0b, 5./6, 1./6); // Top side gradient
 gradientY(target, 0, X, y0b, 3, red/4.f, red/2.f); // Red line
 fill(target, int2(0, y1w), int2(X, 1), 1./4); // Bottom line
 for(uint key=0; key<88; key++) {
  int x0 = margin + key*dx;
  int x1 = x0 + dx;
  int notch[12] = { 3, 1, 4, 0, 1, 2, 1, 4, 0, 1, 2, 1 };
  int l = notch[key%12], r = key!=87 ? notch[(key+1)%12] : 0;
  //fill(target, int2(x0, l==1?y0b:y0w), int2(1, y1b-1-(l==1?y0b:y0w)), color==white?black:color); // Vertical edge top part (previous color)
  bool pressed = left.contains(key+21) || right.contains(key+21) || unknown.contains(key+21);
  const bgr3f color = left.contains(key+21) ? red : right.contains(key+21) ? green : unknown.contains(key+21) ? blue : (l==1 ? black : white);
  const int S = 4;
  if(key==0) l=0; //A-1 has no left notch
  if(l==1) { // black key
   fill(target, int2(x0+0, y0b), int2(1, y1b-y0b), color); // Left vertical edge
   fill(target, int2(x0+1, y0b), int2(1, y1br-y0b), ::min(bgr3f(1), color+bgr3f(1.f/8))); // Left vertical edge (highlight)
   fill(target, int2(x0+2, y0b), int2(dx-2, y1br-y0b), color); // Key
   //if(!pressed) fill(target, int2(x0+1, y1br-1), int2(dx, 1), ::min(bgr3f(1), color+bgr3f(1.f/8))); // Bottom edge (highlight)
   fill(target, int2(x0+1, y1br), int2(dx, y1b-1-y1br), ::min(bgr3f(1), color+bgr3f(1.f/32))); // Bottom side
   fill(target, int2(x0+1, y1b-1), int2(dx-1, 1), color); // Bottom edge (edge)
  } else { // White key
   fill(target, int2(x0, y0w), int2(dx, y1b-y0w), color); // White key top
   if(l!=1) gradientY(target, x0+1, dx, y0w, S, 1.f/2, 3.f/4, 1, Mul); // Top shadow
   //if(r) fill(target, int2(x0+dx, y1b-1), int2(1, 1), 1.f/4*color); // Corner with black
   if(l==0) fill(target, int2(x0, l==1?y0b:y0w), int2(1, y1b-1-(l==1?y0b:y0w)), black); // Vertical edge top part
   fill(target, int2(x0-l*dx/6, y1b-1), int2(1, y1w-(y1b-1)), black); // Left vertical edge (black to bottom)
   int left = l*dx/6;
   int wx0 = x0+1-left; // Left notch
   int right = (6-r)*dx/6;
   int dx1 = dx+right; // Right notch
   fill(target, int2(wx0, y1b), int2(dx1, (pressed?y1w:y1wr)-y1b), color); // white key bottom
   if(left) gradientY(target, wx0, x0-wx0, y1b, S, 1.f/2, 3.f/4, 1, Mul); // Left Black/White H shadow
   if(right) gradientY(target, x0+dx, right, y1b, S, 1.f/2, 3.f/4, 1, Mul); // Right Black/White H shadow
   if(!pressed) {
    fill(target, int2(wx0, y1wr), int2(dx1, 1), 1./4); // white key bottom edge
    fill(target, int2(wx0, y1wr+1), int2(dx1, y1w-(y1wr+1)), 1./2); // white key bottom side
   }
   if(l) {
    gradientX(target, x0, S, y0w, y1b-y0w, 1.f/2, 3.f/4, 1, Mul); // Black|White vertical shadow
    gradientR(target, x0, S, y1b, S, 1.f/2, 3.f/4, 1, Mul); // Black|White radial shadow
    //fill(target, int2(x0, y1b-1), int2(1, 1), 1.f/2*color); // Corner with black
   }
   // Right edge will be next left edge
  }
  if(key==87) { //C7 has no right notch
   fill(target, int2(x1+dx/2, 0), int2(1, y1w), color);
   fill(target, int2(x1, 0), int2(x1+dx/2, y1b-1), color);
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
