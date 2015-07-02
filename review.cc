#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"
#include "variant.h"

struct Rename {
 Rename() {
  Folder results {"."_};
  for(string name: results.list(Files)) {
   if(!endsWith(name,".result")) {
    if(endsWith(name,".failed")||endsWith(name,".working")) {
     log("-", name);
     //remove(name);
    } else
     log("Unknown file", name);
    continue;
   }
   Dict parameters = parseDict(name);
   //parameters.keys.replace("subStepCount"__, "Substep count"__);
   String newName = str(parameters)+".result";
   if(name != newName) {
    log(name, newName);
    //rename(name, newName, results);
   }
  }
 }
};

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 float min = inf, max = -inf;
 uint textSize;
 vec2 headerCellSize = vec2(160/*80*/*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 buffer<string> dimensions[2] = {
  split("Substep count,Friction,Elasticity",","),
  split("Rate,Height,Radius",",")
 };

 ArrayView(string valueName, uint textSize=16)
  : valueName(valueName), textSize(textSize) {
  Folder results ("."_);
  for(string name: results.list(Files)) {
   if(!endsWith(name,".result")) continue;
   Dict configuration = parseDict(name);
   array<array<float>> dataSets;
   TextData s (readFile(name));
   s.until('\n'); // First line: Grain count, Wire count, Wire density
   while(s) {
    for(size_t i = 0; s && !s.match('\n'); i++) {
     float decimal = s.decimal();
     assert_(isNumber(decimal));
     if(i>=dataSets.size) dataSets.append();
     dataSets[i].append( decimal );
     s.whileAny(' ');
    }
   }
   //static ref<string> names {"Load", "Height", "Tension energy", "stretch"};
   float height = dataSets[1][0];
   //if(points.contains(configuration)) { log("Duplicate configuration", configuration); continue; }
   points.insert(move(configuration), height);
  }
  min = ::min(points.values);
  max = ::max(points.values);
 }

 /// Returns coordinates along \a dimension occuring in points matching \a filter
 array<Variant> coordinates(string dimension, const Dict& filter) const {
  array<Variant> allCoordinates;
  for(const Dict& coordinates: points.keys) if(coordinates.includes(filter)) {
   //assert_(coordinates.contains(dimension), coordinates, dimension);
   if(!allCoordinates.contains(coordinates.at(dimension)))
    allCoordinates.insertSorted(copy(coordinates.at(dimension)));
  }
  return allCoordinates;
 }
 /// Returns coordinates occuring in \a points
 map<String, array<Variant> > coordinates(const map<Dict, float>& points) const {
  map<String, array<Variant>> allCoordinates;
  for(const Dict& coordinates: points.keys) for(const auto coordinate: coordinates)
   if(!allCoordinates[copy(coordinate.key)].contains(coordinate.value)) allCoordinates.at(coordinate.key).insertSorted(copy(coordinate.value));
  return allCoordinates;
 }
 /// Returns number of cells for the given \a axis, \a level and \a coordinates
 int cellCount(uint axis, uint level, Dict& filter) const {
  if(level == dimensions[axis].size) return 1;
  string dimension = dimensions[axis][level];
  int cellCount = 0;
  assert_(!filter.contains(dimension));
  for(const Variant& coordinate: coordinates(dimension, filter)) {
   filter[copyRef(dimension)] = copy(coordinate);
   cellCount += this->cellCount(axis, level+1, filter);
  }
  filter.remove(dimension);
  return cellCount;
 }
 int cellCount(uint axis, uint level=0) const { Dict filter; return cellCount(axis, level, filter); }
 int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
 int2 levelCount() { return int2(dimensions[0].size,dimensions[1].size); }
 vec2 sizeHint(vec2) override {
  return vec2(levelCount().yx()+int2(1)) * headerCellSize
              + vec2(cellCount()) * contentCellSize;
 }

 uint renderHeader(Graphics& graphics, vec2 viewSize, vec2 contentCellSize, uint axis, uint level, Dict& filter, uint offset=0) {
  if(level==dimensions[axis].size) return 1;
  string dimension = dimensions[axis][level];
  assert_(!filter.contains(dimension));
  uint cellCount = 0;
  auto coordinates = this->coordinates(dimension, filter);
  for(const Variant& coordinate: coordinates) {
   assert_(coordinate);
   filter[copyRef(dimension)] = copy(coordinate);
   uint childCellCount = renderHeader(graphics, viewSize, contentCellSize, axis, level+1, filter, offset+cellCount);
   vec2 headerOrigin (dimensions[!axis].size+1, level);
   vec2 origin (offset+cellCount, 0);
   vec2 size (childCellCount, 1);
   if(axis) headerOrigin=headerOrigin.yx(), origin=origin.yx(), size=size.yx();
   vec2 cellSize = axis ? vec2(headerCellSize.x, contentCellSize.y) : vec2(contentCellSize.x, headerCellSize.y);
   origin = headerOrigin*headerCellSize + origin*cellSize;
   if(level<dimensions[axis].size-1) {
    int width = dimensions[axis].size-1-level;
    if(!axis) graphics.fills.append(origin+vec2(-width/2,0), vec2((width+1)/2, viewSize.y));
    if(axis) graphics.fills.append(origin+vec2(0,-width/2), vec2(viewSize.x,(width+1)/2));
   }
   //String label = copy(coordinate);
   //if(label[0] < 16) label.removeAt(0); // Removes sort key
   graphics.graphics.insert(origin, Text(str(coordinate), textSize, 0, 1, 0,
                                         "DejaVuSans", true, 1, 0).graphics(vec2(size*cellSize)));
   cellCount += childCellCount;
  }
  filter.remove(dimension);
  return cellCount;
 }

 int renderCell(Graphics& graphics, vec2 cellSize, uint axis, uint level, Dict& filterX, Dict& filterY, vec2 origin=0) {
  if(level==dimensions[axis].size) {
   if(axis==0) { renderCell(graphics, cellSize, 1, 0, filterX, filterY, origin); } // Descends dimensions tree on other array axis
   else { // Renders cell
    for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
     //Image cell = clip(target, );
     //const Variant& point = points.at(coordinates);
     //assert_(isDecimal(point), point);
     //float value = point;
     float value = points.at(coordinates);
     float v = max>min ? (value-min)/(max-min) : 0;
     assert_(v>=0 && v<=1, v, value, min, max);
     //fill(cell, Rect(cell.size()), vec3(0,1-v,v));
     float realValue = value; //abs(value); // Values where maximum is best have been negated
     String text = str(realValue); //point.isInteger?dec(realValue):ftoa(realValue);
     if(value==max) text = bold(text);
     vec2 cellOrigin (vec2(levelCount().yx()+int2(1))*headerCellSize+origin*cellSize);
     graphics.graphics.insert(cellOrigin, Text(text, textSize, 0, 1, 0,
                                               "DejaVuSans", true, 1, 0).graphics(cellSize));
     break;
    }
   }
   return 1;
  }
  string dimension = dimensions[axis][level];
  Dict& filter = axis ? filterY : filterX;
  assert_(!filter.contains(dimension));
  int offset = 0;
  for(const Variant& coordinate: coordinates(dimension, filter)) {
   filter[copyRef(dimension)] = copy(coordinate);
   int childCellCount = renderCell(graphics, cellSize, axis, level+1, filterX, filterY, origin+vec2(axis?0:offset,axis?offset:0));
   offset += childCellCount;
  }
  filter.remove(dimension);
  return offset;
 }

 shared<Graphics> graphics(vec2 size) override {
  assert_(cellCount(), cellCount());
  vec2 cellSize = (size - vec2(levelCount().yx()+int2(1))*headerCellSize ) / vec2(cellCount());
  // Fixed coordinates in unused top-left corner
  array<char> fixed;
  for(const auto& coordinate: coordinates(points)) if(coordinate.value.size==1) fixed.append(coordinate.key+": "_+str(coordinate.value)+"\n"_);
  shared<Graphics> graphics;
  graphics->graphics.insert(vec2(0,0), Text(fixed, textSize).graphics(
                            vec2(dimensions[1].size,dimensions[0].size)*headerCellSize));
  // Value name in unused top-left cell
  graphics->graphics.insert(vec2(dimensions[1].size,dimensions[0].size)*headerCellSize,
    Text(bold(valueName), textSize).graphics(headerCellSize));
  // Dimensions
  for(uint axis: range(2)) for(uint level: range(dimensions[axis].size)) {
   vec2 origin (dimensions[!axis].size-1+1, level);
   if(axis) origin=origin.yx();
   string dimension = dimensions[axis][level];
   graphics->graphics.insert(origin*headerCellSize, Text(dimension,textSize, 0, 1, 0,
                                                         "DejaVuSans", true, 1, 0)
                             .graphics(headerCellSize));
  }

  // Content
  Dict filterX, filterY; renderCell(graphics, cellSize, 0, 0, filterX, filterY);

  // Headers (and lines over fills)
  for(uint axis: range(2)) { Dict filter; renderHeader(graphics, size, cellSize, axis, 0, filter); }
  return graphics;
 }
};

struct Review {
 ArrayView view {"Height"};
 unique<Window> window = ::window(&view, int2(0, 720));
} app;
