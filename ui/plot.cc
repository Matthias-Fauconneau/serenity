#include "plot.h"
#include "graphics.h"
#include "text.h"
#include "color.h"

struct Ticks { float max; uint tickCount; };
uint subExponent(float& value) {
 float subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
 for(auto a: (float[][2]){{1,5},{1.2,6},{1.4,7},{1.6,8},{1.8,9},{2,10},{2.5,5},{3,3},{3.2,8},{4,8},{5,5},{6,6},{8,8},{10,5}}) {
  if(a[0] >= subExponent-0x1p-52) {
   value=(value>0?1:-1)*a[0]*exp10(floor(log10(abs(value))));
   return a[1];
  }
 }
 error("No matching subexponent for"_, value);
}

vec2 Plot::sizeHint(vec2 size) {
 if(!dataSets) return 0;
 if(!size) size = 1024;
 if(plotCircles) return ::min(size.x, size.y); // FIXME: margin.x > margin.y
 else size.y = ::min(size.y, 3*size.x/4);
 return size;
}
shared<Graphics> Plot::graphics(vec2 size) {
 vec2 min=vec2(+__builtin_inf()), max=vec2(-__builtin_inf());
 // Computes axis scales
 for(const auto& data: dataSets.values) {
  for(auto point: data) {
   vec2 p(point.key,point.value);
   if(!isNumber(p)) continue;
   //assert_(isNumber(p.x) && isNumber(p.y), p);
   min=::min(min,p);
   max=::max(max,p);
  }
 }
 for(size_t i: range(2)) if(!log[i]) { if(i>0 && min[i]>0) min[i] = 0; if(max[i]<0) max[i] = 0; }
 if(this->min.x < this->max.x) min.x=this->min.x, max.x=this->max.x; // Custom scales
 if(this->min.y < this->max.y) min.y=this->min.y, max.y=this->max.y; // Custom scales

 shared<Graphics> graphics;
 if(!(min.x < max.x && min.y < max.y)) return graphics;
 assert_(isNumber(min) && isNumber(max) && min.x < max.x && min.y < max.y, min, max);

 int tickCount[2]={};
 for(size_t axis: range(2)) { // Ceils maximum using a number in the preferred sequence
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
    } else min[axis] = 0;
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
 struct Tick : Text { float value; Tick(float value, string label) : Text(label), value(value) {} };
 array<Tick> ticks[2]; vec2 tickLabelSize = 0;
 for(size_t axis: range(2)) {
  uint precision = ::max(1., ceil(-log10(::max(-min[axis],max[axis])/tickCount[axis])));
  for(size_t i: range(tickCount[axis]+1)) {
   float lmin = log[axis] ? log2(min[axis]) : min[axis];
   float lmax = log[axis] ? log2(max[axis]) : max[axis];
   float value = lmin+(lmax-lmin)*i/tickCount[axis];
   if(log[axis]) value = exp2(value);
   String label = str(value, precision, 3u/*value>=1e3 ? 3u : value <=10e-2 ? 1u : 0u*/);
   assert(label);
   ticks[axis].append(value, label);
   tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
  }
 }

 // Evaluates margins
 int left=tickLabelSize.x*3./2, top=tickLabelSize.y, bottom=tickLabelSize.y;
 int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(bold(xlabel)).sizeHint().x);
 //left=right=top=bottom=::max(::max(::max(left, right), top), bottom);
 {int margin = (left+right)-(top+bottom);
  top += margin/2;
  bottom += margin/2;
 }
 const int tickLength = 4;

 // Evaluates colors
 buffer<bgr3f> colors(dataSets.size());
 if(colors.size<=4) for(size_t i: range(colors.size)) colors[i] = ref<bgr3f>{black,red,green,blue}[i];
 else for(size_t i: range(colors.size))
  colors[i] = clamp(bgr3f(0), LChuvtoBGR(53,179, 2*PI*i/colors.size), bgr3f(1));

 // Draws plot
 int2 pen = 0;

 {Text text(bold(name),16); graphics->graphics.insert(vec2(pen+int2((size.x-text.sizeHint().x)/2,top)), text.graphics(0)); pen.y+=text.sizeHint().y; } // Title
 if(legendPosition&1) pen.x += size.x-right;
 else pen.x += left+2*tickLength;
 if(legendPosition&2) {
  pen.y += size.y-bottom-tickLabelSize.y/2;
  for(size_t i: range(dataSets.size())) pen.y -= Text(dataSets.keys[i], 16).sizeHint().y;
 } else {
  pen.y += top;
 }
 for(size_t i: range(dataSets.size())) { // Legend
  Text text(dataSets.keys[i], 16, colors[i]); graphics->graphics.insert(vec2(pen+int2(legendPosition&1 ? -text.sizeHint().x : 0,0)), text.graphics(0)); pen.y+=text.sizeHint().y;
 }

 // Transforms data positions to render positions
 auto point = [&](vec2 p)->vec2{
  // Converts min/max to log (for point(vec2)->vec2)
  for(int axis: range(2)) {
   float lmin = log[axis] ? log2(min[axis]) : min[axis];
   float lmax = log[axis] ? log2(max[axis]) : max[axis];
   if(log[axis]) p[axis] = log2(p[axis]);
   p[axis] = (p[axis]-lmin)/(lmax-lmin);
  }
  return vec2(left+p.x*(size.x-left-right),/*2**/top+(1-p.y)*(size.y-/*2**/top-bottom));
 };

 // Draws axis and ticks
 {vec2 O=vec2(min.x, min.y>0 ? min.y : max.y<0 ? max.y : 0), end = vec2(max.x, O.y); // X
  graphics->lines.append(point(O), point(end));
  for(size_t i: range(tickCount[0]+1)) {
   Tick& tick = ticks[0][i];
   vec2 p(point(vec2(tick.value, O.y)));
   graphics->lines.append(p, p+vec2(0,-tickLength));
   graphics->graphics.insertMulti(p + vec2(-tick.sizeHint().x/2, -min.y > max.y ? -tick.sizeHint().y : 0), tick.graphics(0));
  }
  {Text text(bold(xlabel),16); graphics->graphics.insert(vec2(int2(point(end))+int2(tickLabelSize.x/2, -text.sizeHint().y/2)), text.graphics(0)); }
 }
 {vec2 O=vec2(min.x>0 ? min.x : max.x<0 ? max.x : 0, min.y), end = vec2(O.x, max.y); // Y
  graphics->lines.append(point(O), point(end));
  for(size_t i: range(tickCount[1]+1)) {
   vec2 p (point(O+(i/float(tickCount[1]))*(end-O)));
   graphics->lines.append(p, p+vec2(tickLength,0));
   Text& tick = ticks[1][i];
   graphics->graphics.insert(p + vec2(-tick.sizeHint().x-left/6, -tick.sizeHint().y/2), tick.graphics(0));
  }
  {Text text(bold(ylabel),16); graphics->graphics.insert(vec2(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y/2)), text.graphics(0));}
 }

 // Plots data points
 for(size_t i: range(dataSets.size())) {
  bgr3f color = colors[i];
  assert_(bgr3f(0) <= color && color <= bgr3f(1), color);
  const auto& data = dataSets.values[i];
  buffer<vec2> points = apply(data.size(), [&](size_t i){ return point( vec2(data.keys[i],data.values[i]) ); });
  if(plotPoints || points.size==1 || plotBandsX || plotBandsY) for(vec2 p: points) {
   if(!isNumber(p)) continue;
   const int pointRadius = 2;
   graphics->lines.append(p-vec2(pointRadius, 0), p+vec2(pointRadius, 0), color);
   graphics->lines.append(p-vec2(0, pointRadius), p+vec2(0, pointRadius), color);
  }
  if(plotLines && !(plotBandsX || plotBandsY)) for(size_t i: range(points.size-1)) {
   if(!isNumber(points[i]) || !isNumber(points[i+1])) continue;
   graphics->lines.append(points[i], points[i+1], color);
  }
  if(plotBandsY && points) { // Y bands
   //vec2 O = point(vec2(0,0));
   TrapezoidY::Span span[2] = {/*{O.x,O.y,O.y}*/{points[0].x,points[0].y,points[0].y},{points[0].x,points[0].y,points[0].y}};
   for(vec2 p: points.slice(1)) {
    if(p.x == span[1].x) {
     span[1].min = ::min(span[1].min, p.y);
     span[1].max = ::max(span[1].max, p.y);
    } else { // Commit band
     graphics->lines.append(vec2(span[0].x, span[0].min), vec2(span[1].x, span[1].min), color);
     graphics->lines.append(vec2(span[0].x, span[0].max), vec2(span[1].x, span[1].max), color);
     graphics->trapezoidsY.append(span[0], span[1], color, 1.f/2);
     span[0] = span[1];
     span[1] = {p.x, p.y, p.y};
    }
   }
   graphics->lines.append(vec2(span[0].x, span[0].min), vec2(span[1].x, span[1].min), color);
   graphics->lines.append(vec2(span[0].x, span[0].max), vec2(span[1].x, span[1].max), color);
   graphics->trapezoidsY.append(span[0], span[1], color, 1.f/2);
  }
  {
   map<float, float> sortY;
   for(auto p: data) sortY.insertSortedMulti(p.value, p.key);
   buffer<vec2> points = apply(sortY.size(), [&](size_t i){ return point(vec2(sortY.values[i],sortY.keys[i]) ); });

   if(plotBandsX && points) { // X bands
    //vec2 O = point(vec2(0,0));
    TrapezoidX::Span span[2] = {/*{O.y,O.x,O.x}*/{points[0].y,points[0].x,points[0].x}, {points[0].y,points[0].x,points[0].x}};
    for(vec2 p: points.slice(1)) {
     if(p.y == span[1].y) {
      span[1].min = ::min(span[1].min, p.x);
      span[1].max = ::max(span[1].max, p.x);
     } else { // Commit band
      graphics->lines.append(vec2(span[0].min, span[0].y), vec2(span[1].min, span[1].y), color);
      graphics->lines.append(vec2(span[0].max, span[0].y), vec2(span[1].max, span[1].y), color);
      graphics->trapezoidsX.append(span[0], span[1], color, 1.f/2);
      span[0] = span[1];
      span[1] = {p.y, p.x, p.x};
     }
    }
    graphics->lines.append(vec2(span[0].min, span[0].y), vec2(span[1].min, span[1].y), color);
    graphics->lines.append(vec2(span[0].max, span[0].y), vec2(span[1].max, span[1].y), color);
    graphics->trapezoidsX.append(span[0], span[1], color, 1.f/2);
   }
   if(plotCircles) {
    float xSum = 0, N = 0;
    for(size_t i : range(sortY.size())) {
     const float x = sortY.values[i], y = sortY.keys[i];

     xSum += x;
     N++;
     {
      const float x = xSum / N; xSum=0; N=0;
      const vec2 O = point(vec2(x, 0/*data.values[0]*/));
      float Rx = abs(point(vec2(x+y/2,0)).x - point(vec2(x-y/2,0)).x);
      float Ry = abs(point(vec2(x, y/*p.key*/)).y - O.y);
      const float N = 64;
      for(float i: range(N/2)) {
       graphics->lines.append(
          O+vec2(Rx*cos(2*PI*i/N), -Ry*sin(2*PI*i/N)),
          O+vec2(Rx*cos(2*PI*(i+1)/N), -Ry*sin(2*PI*(i+1)/N)), color, 1.f/colors.size);
      }
     }
    }
   }
  }
 }
 {
  float x = max.x;
  map<int, int> done;
  for(size_t i: range(fits.size())) for(auto f: fits.values[i]) {
   vec2 B = point(vec2(x, f.a*x+f.b));
   graphics->lines.append(point(vec2(0,f.b)), B, colors[i]);
   int value = round(atan(f.a, 1)*180/PI);
   if(!done[value]) {
    done[value]++;
    Text text(str(value)+"°", 16, colors[i]);
    graphics->graphics.insert(B+vec2(16, -text.sizeHint().y/2), text.graphics(0));
   }
  }
 }
 return graphics;
}
