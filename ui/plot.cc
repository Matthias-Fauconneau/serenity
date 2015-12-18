#include "plot.h"
#include "graphics.h"
#include "text.h"
#include "color.h"

static inline double exp2(double x) { return __builtin_exp2(x); }
static inline double log2(double x) { return __builtin_log2(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double exp10(double x) { return __builtin_exp2(__builtin_log2(10)*x); }

struct Ticks { float max; uint tickCount; };
uint subExponent(float& value) {
 float subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
 for(auto a: (float[][2]){{1,5},{1.2,6},{1.4,7},{1.6,4},{2,2},{2.5,5},{3,3},{3.2,8},{4,4},{5,5},{6,6},{8,8},{10,5}}) {
  if(a[0] >= subExponent-0x1p-52) {
   value=(value>0?1:-1)*a[0]*exp10(floor(log10(abs(value))));
   return a[1];
  }
 }
 error("No matching subexponent for"_, value);
}

vec2 Plot::sizeHint(vec2 size) {
 if(!dataSets) return 0;
 //if(!size) size = 1024;
 size.x = ::min(size.x, 1680.f/5);
 size.y = ::min(size.y, 1680.f/5);
 if(plotCircles || uniformScale) return -::min(size.x, size.y); // FIXME: margin.x > margin.y
 else size.y = ::min(size.y, 3*size.x/4);
 return -size; // Expanding
}
shared<Graphics> Plot::graphics(vec2 size) {
 vec2 min=vec2(+__builtin_inf()), max=vec2(-__builtin_inf());
 // Computes axis scales
 for(const map<float,float>& data: dataSets.values) {
  if(!data.values) continue; // FIXME
  for(auto point: data) {
   vec2 p(point.key, point.value);
   if(!isNumber(p)) continue;
   //assert_(isNumber(p.x) && isNumber(p.y), p);
   min=::min(min,p);
   max=::max(max,p);
  }
 }
 for(size_t i: range(2)) if(!log[i]) { if(i>0 && min[i]>0) min[i] = 0; if(max[i]<0) max[i] = 0; }
 if(this->min.x < this->max.x) min.x=this->min.x, max.x=this->max.x; // Custom scales
 if(this->min.y < this->max.y) min.y=this->min.y, max.y=this->max.y; // Custom scales
 //min = 0;

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

 const float textSize = 10; const string fontName = "latinmodern-math"_;
 // Configures ticks
 struct Tick : Text { float value; Tick(float value, string label, float textSize, string fontName) : Text(label, textSize, 0,1,0, fontName), value(value) {} };
 array<Tick> ticks[2]; vec2 tickLabelSize = 0;
 for(size_t axis: range(2)) {
  uint precision = ::max(1.f, ceil(-log10(::max(-min[axis],max[axis])/tickCount[axis])));
  for(size_t i: range(tickCount[axis]+1)) {
   float lmin = log[axis] ? log2(min[axis]) : min[axis];
   float lmax = log[axis] ? log2(max[axis]) : max[axis];
   float value = lmin+(lmax-lmin)*i/tickCount[axis];
   if(log[axis]) value = exp2(value);
   String label = str(value, precision, 3u/*value>=1e3 ? 3u : value <=10e-2 ? 1u : 0u*/);
   assert(label);
   ticks[axis].append(value, label, textSize, fontName);
   tickLabelSize = ::max(tickLabelSize, vec2(ticks[axis][i].sizeHint().x, textSize));
  }
 }

 // Evaluates margins
 float left = tickLabelSize.x+textSize; //::max(Text(xlabel, textSize, 0,1,0, fontName).sizeHint().x/2, tickLabelSize.x+textSize);
 float top=tickLabelSize.y*3/2;
 float bottom=(tickLabelSize.y+textSize)*3/2;
 float right = tickLabelSize.x/2;//+Text(xlabel, textSize, 0,1,0, fontName).sizeHint().x;
 if(fits) right += Text("000", textSize, 0,1,0, fontName).sizeHint().x;

 //left=right=top=bottom=::max(::max(::max(left, right), top), bottom);
 if(plotCircles || uniformScale) {
  //assert_(max.x == max.y, max);
  /*float W = size.x-(left+right), H = size.y-(top+bottom);
  float margin = -;
  if(margin > 0) {
   top += margin/2;
   bottom += margin/2;
  } else {
   left -= margin/2;
   right -= margin/2;
  }*/
  float h = size.y - (size.x-(left+right)) * max.y/max.x;
  //assert_(h > top+bottom);
  int margin = (h-(top+bottom))/2;
  top += margin;
  bottom += margin;
  //assert_(top+bottom == h, top+bottom, h);
  /*assert_( (size.y-(top+bottom))*max.x == (size.x-(left+right))*max.y,
           size.y, top+bottom, size.y-(top+bottom),
           size.x, left+right, size.x-(left+right));*/
 }
 const float tickLength = 4;

 // Evaluates colors
 buffer<bgr3f> colors(dataSets.size());
 if(colors.size<=4) for(size_t i: range(colors.size)) colors[i] = ref<bgr3f>{black,red,green,blue}[i];
 else for(size_t i: range(colors.size))
  colors[i] = clamp(bgr3f(0), LChuvtoBGR(53,179, 2*PI*i/colors.size), bgr3f(1));

 // Draws plot
 int2 pen = 0;

 if(name) { // Title
  Text text(bold(name), textSize, 0,1,0, fontName);
  graphics->graphics.insert(vec2(pen+int2((size.x-text.sizeHint().x)/2,top)), text.graphics(0));
  pen.y+=text.sizeHint().y;
 }
 if(legendPosition&1) pen.x += size.x - right;
 else pen.x += left + 2*tickLength;
 if(legendPosition&2) {
  pen.y += size.y - bottom - tickLabelSize.y/2;
  //for(size_t /: range(dataSets.size())) pen.y -= textSize; //Text(dataSets.keys[i], textSize, 0,1,0, fontName).sizeHint().y;
  pen.y -= dataSets.size()*textSize;
 } else {
  pen.y += top;
 }
 /*if(dataSets.size() <= 1)*/ //for(size_t i: range(dataSets.size())) { // Legend
 for(size_t i: reverse_range(dataSets.size())) { // Legend
  Text text(dataSets.keys[i], textSize, colors[i], 1,0, fontName);
  graphics->graphics.insert(vec2(pen+int2(legendPosition&1 ? -text.sizeHint().x : 0,0)), text.graphics(0));
  pen.y += textSize; //text.sizeHint().y;
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
   graphics->lines.append(p, p+vec2(0,tickLength));
   graphics->graphics.insertMulti(p + vec2(-tick.sizeHint().x/2, /*-min.y > max.y ? -tick.sizeHint().y :*/  -tick.sizeHint().y/2+textSize), tick.graphics(0));
  }
  {Text text(/*bold*/(xlabel),textSize, 0,1,0, fontName);
   graphics->graphics.insert(vec2((left+(size.x-right))/2-text.sizeHint().x/2, size.y-bottom-textSize), text.graphics(0)); }
 }
 {vec2 O=vec2(min.x>0 ? min.x : max.x<0 ? max.x : 0, min.y), end = vec2(O.x, max.y); // Y
  graphics->lines.append(point(O), point(end));
  for(size_t i: range(tickCount[1]+1)) {
   vec2 p (point(O+(i/float(tickCount[1]))*(end-O)));
   graphics->lines.append(p, p+vec2(-tickLength,0));
   Text& tick = ticks[1][i];
   graphics->graphics.insert(p + vec2(-tick.sizeHint().x-textSize, -tick.sizeHint().y/2), tick.graphics(0));
  }
  {Text text(/*bold*/(ylabel),textSize, 0,1,0, fontName);
   vec2 p(int2(point(end))+int2(-text.sizeHint().x/2, /*-text.sizeHint().y-tickLabelSize.y/2*/-text.sizeHint().y/2-textSize));
   p.x = ::max(0.f, p.x);
   graphics->graphics.insert(p, text.graphics(0));}
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
   if(i==0) {
    graphics->lines.append(p-vec2(pointRadius, 0), p+vec2(pointRadius, 0), color);
    graphics->lines.append(p-vec2(0, pointRadius), p+vec2(0, pointRadius), color);
   } else if(i==1) {
    float d = pointRadius/sqrt(2.);
    graphics->lines.append(p+vec2(-d, -d), p+vec2(d, d), color);
    graphics->lines.append(p+vec2(-d, d), p+vec2(d, -d), color);
   } else if(i==2) {
    float d = pointRadius/sqrt(2.);
    graphics->lines.append(p+vec2(-d, -d), p+vec2(d, -d), color);
    graphics->lines.append(p+vec2(d, -d), p+vec2(d, d), color);
    graphics->lines.append(p+vec2(d, d), p+vec2(-d, d), color);
    graphics->lines.append(p+vec2(-d, d), p+vec2(-d, -d), color);
   } else if(i==3) {
    float d = pointRadius;
    graphics->lines.append(p-vec2(0, -d), p+vec2(d, 0), color);
    graphics->lines.append(p-vec2(d, 0), p+vec2(0, d), color);
    graphics->lines.append(p-vec2(0, d), p+vec2(-d, 0), color);
    graphics->lines.append(p-vec2(-d, 0), p+vec2(0, -d), color);
   }
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
    for(size_t i : range(/*sortY*/circles.size())) {
     const float x = /*sortY*/circles.keys[i], y = /*sortY*/circles.values[i];

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
  float minY = inff;
  //extern bool hack;
  for(size_t i: range(/*hack*/0, fits.size())) for(auto f: fits.values[i]) {
   float y = f.a*x+f.b;
   vec2 A = point(vec2(0,f.b));
   vec2 B = point(vec2(x, y));
   if(fits.size() == 1)
    graphics->lines.append(point(vec2(0,f.b)), B, colors[i]);
   else {
    //vec2 A = point(vec2(x,f.a*x + f.b));
    //vec2 B = point(vec2(x1, f.a*x1 + f.b));
    float l = (B.x-A.x)/16;
    float o = (i+1)*l/4; //l/(i+1);
    for(float x=A.x; x<B.x-l; x+=l) {
     graphics->lines.append(vec2(x, A.y+(x-A.x)/(B.x-A.x)*(B.y-A.y)), vec2(x+o, A.y+((x+o)-A.x)/(B.x-A.x)*(B.y-A.y)), colors[i]);
    }
   }
   B.y = ::min(minY, B.y);
   minY = B.y - textSize;
   int a = round(atan(f.a, 1)*180/PI);
   //int b = round(f.b);
   if(/*!hack*/ /*&& !done[a]*/ plotAngles) {
    done[a]++;
    Text text(str(a)+"°"_, textSize, colors[i], 1,0, fontName);
    graphics->graphics.insert(B+vec2(0/*textSize*/, -text.sizeHint().y/2), text.graphics(0));
   }
   const auto& data = dataSets.values[i];
   if(plotPoints) for(auto p: data) {
    float x = p.key, y = p.value;
    float x2 = x + f.a/(f.a*f.a+1)*(y-f.b-f.a*x);
    float y2 = f.a*x2 + f.b;
    graphics->lines.append(point(vec2(x,y)), point(vec2(x2, y2)), colors[i]);
   }
  }
 }
 return graphics;
}
