#include "plot.h"
#include "graphics.h"
#include "text.h"
#include "color.h"

struct Ticks { float max; uint tickCount; };
uint subExponent(float& value) {
 real subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
 for(auto a: (real[][2]){{1,5}, {1.2,6}, {1.25,5}, {1.6,8}, {2,10}, {2.5,5}, {3,3}, {4,8}, {5,5}, {6,6}, {8,8}, {9.6,8}, {10,5}}) {
  if(a[0] >= subExponent-0x1p-52) { value=(value>0?1:-1)*a[0]*exp10(floor(log10(abs(value)))); return a[1]; }
 }
 error("No matching subexponent for"_, value);
}

vec2f Plot::sizeHint(vec2f) { return 1024; }
shared<Graphics> Plot::graphics(vec2f size) {
 vec2f min=vec2f(+__builtin_inf()), max=vec2f(-__builtin_inf());
 if(this->min.x < this->max.x && this->min.y < this->max.y) min=this->min, max=this->max; // Custom scales
 else {  // Computes axis scales
  assert(dataSets);
  for(const auto& data: dataSets.values) {
   for(auto point: data) {
    vec2f p(point.key,point.value);
    if(!isNumber(p)) continue;
    //assert_(isNumber(p.x) && isNumber(p.y), p);
    min=::min(min,p);
    max=::max(max,p);
   }
  }
  for(uint i: range(2)) if(!log[i]) { if(i>0 && min[i]>0) min[i] = 0; if(max[i]<0) max[i] = 0; }
 }
 assert_(isNumber(min) && isNumber(max) && min.x < max.x && min.y < max.y, min, max);
 shared<Graphics> graphics;
 //if(!(min.x < max.x && min.y < max.y)) return graphics;

 int tickCount[2]={};
 for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
  if(max[axis]>-min[axis]) {
   if(log[axis]) { //FIXME
    max[axis] = exp2(ceil(log2(max[axis])));
    tickCount[axis] = ceil(log2(max[axis]/min[axis]));
    min[axis] = max[axis]*exp2( -tickCount[axis] );
   } else {
    tickCount[axis] = subExponent(max[axis]);
    if(min[axis] < 0) {
     float tickWidth = max[axis]/tickCount[axis];
     min[axis] = floor(min[axis]/tickWidth)*tickWidth;
     tickCount[axis] += -min[axis]/tickWidth;
    }
   }
  } else {
   assert(!log[axis]); //FIXME
   tickCount[axis] = subExponent(min[axis]);
   if(max[axis] > 0) {
    float tickWidth = -min[axis]/tickCount[axis];
    max[axis] = -floor(-max[axis]/tickWidth)*tickWidth;
    tickCount[axis] += max[axis]/tickWidth;
   }
  }
 }

 // Configures ticks
 struct Tick : Text { float value; Tick(float value, string label):Text(label), value(value) {} };
 array<Tick> ticks[2]; vec2f tickLabelSize = 0;
 for(uint axis: range(2)) {
  uint precision = ::max(0., ceil(-log10(::max(-min[axis],max[axis])/tickCount[axis])));
  for(uint i: range(tickCount[axis]+1)) {
   float lmin = log[axis] ? log2(min[axis]) : min[axis];
   float lmax = log[axis] ? log2(max[axis]) : max[axis];
   float value = lmin+(lmax-lmin)*i/tickCount[axis];
   if(log[axis]) value = exp2(value);
   String label = str(value, precision, value>=10e5 ? 3u : value <=10e-2 ? 1u : 0u);
   assert(label);
   ticks[axis].append(value, label);
   tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
  }
 }

 // Evaluates margins
 int left=tickLabelSize.x*3./2, top=tickLabelSize.y, bottom=tickLabelSize.y;
 int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(bold(xlabel)).sizeHint().x);
 const int tickLength = 4;

 // Evaluates colors
 buffer<bgr3f> colors(dataSets.size());
 if(colors.size==1) colors[0] = black;
 else for(uint i: range(colors.size))
  colors[i] = clamp(bgr3f(0), LChuvtoBGR(53,179, 2*PI*i/colors.size), bgr3f(1));

 // Draws plot
 int2 pen = 0;

 {Text text(bold(name),16); graphics->graphics.insert(vec2f(pen+int2((size.x-text.sizeHint().x)/2,top)), text.graphics(0)); pen.y+=text.sizeHint().y; } // Title
 if(legendPosition&1) pen.x += size.x-right;
 else pen.x += left+2*tickLength;
 if(legendPosition&2) {
  pen.y += size.y-bottom-tickLabelSize.y/2;
  for(uint i: range(dataSets.size())) pen.y -= Text(dataSets.keys[i], 16).sizeHint().y;
 } else {
  pen.y += top;
 }
 for(uint i: range(dataSets.size())) { // Legend
  Text text(dataSets.keys[i], 16, colors[i]); graphics->graphics.insert(vec2f(pen+int2(legendPosition&1 ? -text.sizeHint().x : 0,0)), text.graphics(0)); pen.y+=text.sizeHint().y;
 }

 // Transforms data positions to render positions
 auto point = [&](vec2f p)->vec2f{
  // Converts min/max to log (for point(vec2f)->vec2f)
  for(int axis: range(2)) {
   float lmin = log[axis] ? log2(min[axis]) : min[axis];
   float lmax = log[axis] ? log2(max[axis]) : max[axis];
   if(log[axis]) p[axis] = log2(p[axis]);
   p[axis] = (p[axis]-lmin)/(lmax-lmin);
  }
  return vec2f(left+p.x*(size.x-left-right),2*top+(1-p.y)*(size.y-2*top-bottom));
 };

 // Draws axis and ticks
 {vec2f O=vec2f(min.x, min.y>0 ? min.y : max.y<0 ? max.y : 0), end = vec2f(max.x, O.y); // X
  graphics->lines.append(round(point(O)), round(point(end)));
  for(uint i: range(tickCount[0]+1)) {
   Tick& tick = ticks[0][i];
   vec2f p(round(point(vec2f(tick.value, O.y))));
   graphics->lines.append(p, p+vec2f(0,-tickLength));
   graphics->graphics.insertMulti(p + vec2f(-tick.sizeHint().x/2, -min.y > max.y ? -tick.sizeHint().y : 0), tick.graphics(0));
  }
  {Text text(bold(xlabel),16); graphics->graphics.insert(vec2f(int2(point(end))+int2(tickLabelSize.x/2, -text.sizeHint().y/2)), text.graphics(0)); }
 }
 {vec2f O=vec2f(min.x>0 ? min.x : max.x<0 ? max.x : 0, min.y), end = vec2f(O.x, max.y); // Y
  graphics->lines.append(round(point(O)), round(point(end)));
  for(uint i: range(tickCount[1]+1)) {
   vec2f p (round(point(O+(i/float(tickCount[1]))*(end-O))));
   graphics->lines.append(p, p+vec2f(tickLength,0));
   Text& tick = ticks[1][i];
   graphics->graphics.insert(p + vec2f(-tick.sizeHint().x-left/6, -tick.sizeHint().y/2), tick.graphics(0));
  }
  {Text text(bold(ylabel),16); graphics->graphics.insert(vec2f(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y/2)), text.graphics(0));}
 }

 // Plots data points
 for(uint i: range(dataSets.size())) {
  bgr3f color = colors[i];
  assert_(bgr3f(0) <= color && color <= bgr3f(1), color);
  const auto& data = dataSets.values[i];
  buffer<vec2f> points = apply(data.size(), [&](uint i){ return point( vec2f(data.keys[i],data.values[i]) ); });
  if(plotPoints) for(uint i: range(points.size)) {
   vec2f p = round(points[i]);
   if(!isNumber(p)) continue;
   const int pointRadius = 2;
   graphics->lines.append(p-vec2f(pointRadius, 0), p+vec2f(pointRadius, 0), color);
   graphics->lines.append(p-vec2f(0, pointRadius), p+vec2f(0, pointRadius), color);
  }
  if(plotLines) for(uint i: range(data.size()-1)) {
   if(!isNumber(points[i]) || !isNumber(points[i+1])) continue;
   graphics->lines.append(points[i], points[i+1], color);
  }
 }
 return graphics;
}
