#include "layout.h"

// Layout
shared<Graphics> Layout::graphics(vec2 size, Rect clip) {
 array<Rect> widgets = layout(size);
 shared<Graphics> graphics;
 for(size_t i: range(count())) if(widgets[i] & clip) {
  graphics->graphics.insert(vec2(widgets[i].origin()), at(i).graphics(widgets[i].size(), Rect(widgets[i].size()) /*& (clip-origin)*/));
 }
 return graphics;
}

float Layout::stop(vec2 size, int axis, float currentPosition, int direction) {
 array<Rect> widgets = layout(size);
 for(int i: range(widgets.size)) {
  if((i==0 || widgets[i].min[axis] <= currentPosition) && currentPosition < widgets[i].max[axis]) {
   return widgets[clamp(0, i+direction, int(widgets.size-1))].min[axis];
  }
 }
 error(currentPosition);
}

bool Layout::mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) {
 array<Rect> widgets = layout(size);
 for(size_t i: range(widgets.size))
  if(widgets[i].contains(cursor) && at(i).mouseEvent(cursor-widgets[i].origin(), widgets[i].size(), event, button, focus)) return true;
 return false;
}

// Linear
vec2 Linear::sizeHint(const vec2 xySize) {
 size_t count = this->count();
 if(!count) return {};
 const vec2 size = xy(xySize);
 float widths[count];
 float remainingWidth = abs(size.x);
 float expandingWidth = 0;
 float height = 0;
 bool expandingHeight=false;

 for(size_t index: range(count)) {
  vec2 hint = xy(at(index).sizeHint(xySize));
  assert_(isNumber(hint), xySize, index, hint);
  if(hint.x<0) expandingWidth++; // Counts expanding widgets
  widths[index] = abs(hint.x);
  remainingWidth -= abs(hint.x); // Commits minimum width for all widgets (unless evaluating required size for sizeHint)
  if(hint.y<0) expandingHeight=true;
  height = max(height, size.y>0 ? (hint.y < 0 ? size.y : min(size.y, hint.y)) : abs(hint.y));
 }

 // Reduces widgets to fit allocated width
 if(size.x > 0) while(remainingWidth <= -float(count)) { // While layout is overcommited
  float first = max(ref<float>(widths,count)); // First largest size
  int firstCount=0; for(float size: widths) if(size == first) firstCount++; // Counts how many widgets already have the largest size
  assert_(firstCount);
  float second=0; for(float size: widths) if(second<size && size<first) second=size; // Second largest size
  float offset = max(1.f, min(-remainingWidth, first-second) / firstCount); // Distributes reduction to all largest widgets (max(1,...) to account for flooring)
  for(float& size: widths) if(size == first) { size -= offset, remainingWidth += offset; }
 }
 // Evaluates new required size with fitted widgets
 float requiredWidth = 0, requiredHeight = 0;
 for(size_t index: range(count)) {
  vec2 hint = xy(at(index).sizeHint(xy(vec2(widths[index], height))));
  assert_(isNumber(hint), xy(vec2(widths[index], height)));
  requiredWidth += abs(hint.x);
  requiredHeight = max(requiredHeight, abs(hint.y));
 }
 assert_(isNumber(xy(vec2((expandingWidth||expanding?-1:1)*requiredWidth, (expandingHeight?-1:1)*requiredHeight))), expandingHeight, requiredHeight);
 return xy(vec2((expandingWidth||expanding?-1:1)*requiredWidth, (expandingHeight?-1:1)*requiredHeight));
}

buffer<Rect> Linear::layout(const vec2 xySize) {
 size_t count = this->count();
 if(!count) return {};
 const vec2 size = xy(xySize);
 float width = abs(size.x) /*remaining space*/; float expanding=0, height=0;
 float widths[count], heights[count];

 for(size_t index: range(count)) {
  vec2 hint = xy(at(index).sizeHint(xySize));
  widths[index] = hint.x;
  width -= abs(widths[index]); // Commits minimum width for all widgets (unless evaluating required size for sizeHint)
  if(hint.x<0) expanding++; // Counts expanding widgets
  height = max(height, heights[index] = size.y ? (hint.y < 0 ? size.y : min(size.y, hint.y)) : hint.y); // Required height
 }

 int sharing = expanding ?: (main==Share ? count : (main == ShareTight ? count+2 : 0));
 if(sharing && width >= sharing) { // Shares extra space evenly between sharing widgets
  float extra = width/sharing;
  for(size_t i: range(count)) {
   if(!expanding || widths[i]<0) { //if all widgets are sharing or this widget is expanding
    widths[i] = abs(widths[i])+extra, width -= extra; //commits extra space
   }
  }
  //width%sharing space remains as extra is rounded down
 } else if(size.x>0) { // Reduces widgets to fit allocated space
  for(size_t i: range(count)) widths[i]=abs(widths[i]); // Converts all expanding widgets to fixed
  while(width<=-int(count)) { // While layout is overcommited
   float first = max(ref<float>(widths,count)); // First largest size
   int firstCount=0; for(float size: widths) if(size == first) firstCount++; // Counts how many widgets already have the largest size
   assert_(firstCount);
   float second=0; for(float size: widths) if(second<size && size<first) second=size; // Second largest size
   float offset = max(1.f, min(-width, first-second) / firstCount); // Distributes reduction to all largest widgets (max(1,...) to account for flooring)
   for(float& size: widths) if(size == first) { size -= offset, width += offset; }
  }
 }

 float margin = (main==Spread && count>1) ? width/(count-1) : 0; // Spreads any margin between all widgets
 width -= margin*(count-1); //width%(count-1) space remains as margin is rounded down

 if(main==Even) {
  for(size_t i: range(count)) widths[i]=size.x/count; //converts all expanding widgets to fixed
  width = size.x-count*size.x/count;
 }

 vec2 pen;
 if(main==Spread || main==Left) pen.x = 0;
 else if(main==Center || main==Even || main==Share || main==ShareTight) pen.x = width/2;
 else if(main==Right) pen.x = size.x-width;
 else error("main");
 if(side==AlignLeft) pen.y = 0;
 else if(side==ShareTight) { height = (height+size.y)/2; pen.y = (size.y-height)/2; }
 else if(side==AlignCenter) pen.y =(size.y-height)/2;
 else if(side==AlignRight) pen.y = size.y-height;
 else if(side==Expand) { if(size.y) height = size.y; pen.y = 0; } // If not evaluating required size for sizeHint
 else error("side");
 buffer<Rect> widgets(count, 0);
 for(size_t i: range(count)) {
  float y=0;
  if(side==AlignLeft||side==AlignCenter||side==AlignRight||side==Expand||side==ShareTight) heights[i]=height;
  else if(side==Left) y=0;
  else if(side==Center) y=(height-heights[i])/2;
  else if(side==Right) y=height-heights[i];
  widgets.append( Rect::fromOriginAndSize(xy(pen+vec2(0,y)), xy(vec2(widths[i],heights[i]))) );
  pen.x += widths[i]+margin;
 }
 return widgets;
}

// Grid
buffer<Rect> GridLayout::layout(vec2 size, vec2& sizeHint) {
 if(!count()) return {};
 buffer<Rect> widgets(count(), 0);
 int w = this->width, h=0/*this->height*/; for(;;) { if(w*h >= (int)count()) break; if(!this->width && w<=h) w++; else h++; }
 float widths[w], heights[h];
 for(uint x: range(w)) {
  float maxX = 0;
  for(uint y : range(h)) {
   size_t index = y*w+x;
   if(index<count()) maxX = ::max(maxX, abs(at(index).sizeHint(size).x));
  }
  widths[x] = maxX;
 }

 float extraWidth;
 /**/  if(uniformX) {
  const float requiredWidth = max(ref<float>(widths,w)) * w;
  sizeHint.x = requiredWidth;
  const float availableWidth = size.x ?: requiredWidth;
  const float fixedWidth = availableWidth / w;
  for(float& v: widths) v = fixedWidth;
  extraWidth = availableWidth - w*fixedWidth;
 }
 else if(size.x) {
  const float requiredWidth = sum<float>(ref<float>(widths,w));
  sizeHint.x = requiredWidth;
  extraWidth = size.x ? size.x-requiredWidth: 0;
  const float extra = extraWidth / w; // Extra space per column (may be negative for missing space)
  for(float& v: widths) { v += extra; extraWidth -= extra; } // Distributes extra/missing space
 }
 else extraWidth = 0;

 float extraHeight;
 if(uniformY) {
  const float requiredHeight = max(ref<float>(heights,h)) * h;
  sizeHint.y = requiredHeight;
  const float availableHeight = size.y ?: requiredHeight;
  const float fixedHeight = availableHeight / h;
  for(float& v: heights) v = fixedHeight;
  extraHeight = availableHeight - h*fixedHeight;
 } else {
  for(size_t y : range(h)) {
   float maxY = 0;
   for(size_t x: range(w)) {
    size_t index = y*w+x;
    if(index<count()) maxY = ::max(maxY, abs(at(index).sizeHint(vec2(widths[x],size.y)).y));
   }
   heights[y] = maxY;
  }
  const float requiredHeight = sum<float>(ref<float>(heights,h)); // Remaining space after fixed allocation
  sizeHint.y = requiredHeight;
  extraHeight = size.y ? size.y-requiredHeight: 0;
  const float extra = extraHeight / h; // Extra space per cell
  if(extra > 0) {
   for(float& v: heights) { v += extra; extraHeight -= extra; } // Distributes extra space
  } else {
   while(extraHeight <= -h) { // While layout is overcommited
    float first = max(ref<float>(heights, h)); // First largest size
    int firstCount=0; for(float size: heights) if(size == first) firstCount++; // Counts how many widgets already have the largest size
    float second=0; for(float size: heights) if(second<size && size<first) second=size; // Second largest size
    float offset = max(1.f, min(-extraHeight, first-second) / firstCount); // Distributes reduction to all largest widgets
    for(float& size: heights) if(size == first) { size -= offset, extraHeight += offset; }
   }
  }
  assert_(extraHeight > -h, extraHeight, size, "(", ref<float>(widths,w), ")", "(", ref<float>(heights,h),")", sum<float>(ref<float>(heights,h)));
 }

 { // Recomputes widths with given height constraints
  for(uint x: range(w)) {
   float maxX = 0;
   for(uint y : range(h)) {
    size_t index = y*w+x;
    if(index<count()) maxX = ::max(maxX, abs(at(index).sizeHint(vec2(size.x, heights[y])).x));
   }
   widths[x] = maxX;
  }
  if(size.x) {
   const float requiredWidth = sum<float>(ref<float>(widths,w));
   sizeHint.x = requiredWidth;
   extraWidth = size.x ? size.x-requiredWidth: 0;
   const float extra = extraWidth / w; // Extra space per column (may be negative for missing space)
   for(float& v: widths) { v += extra; extraWidth -= extra; } // Distributes extra/missing space
  }
 }

 float Y = extraHeight/2;
 for(size_t y : range(h)) {
  float X = extraWidth/2;
  for(size_t x: range(w)) {
   size_t i = y*w+x;
   if(i<count()) {
    widgets.append( Rect::fromOriginAndSize(vec2(X,Y), vec2(widths[x], heights[y])) );
    X += widths[x];
   }
  }
  Y += heights[y];
  //assert_(size.y ==0 || (vec2(0) < vec2(X,Y) && vec2(X,Y) < size+vec2(w,h)), X, Y, size, ref<float>(widths,w), ref<float>(heights,h));
 }
 return widgets;
}
