#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"
#include "variant.h"

#if 0
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
#endif

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, String> ids; // Job IDs
 map<String, String> running; // Running job IDs, start time
 float min = inf, max = -inf;
 uint textSize;
 vec2 headerCellSize = vec2(80*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 buffer<string> dimensions[2] = {
  split("Friction,Radius,Pressure",","),
  split("TimeStep,Rate,Pattern",",")
 };
 struct Target { Rect rect; const Dict& key; };
 array<Target> targets;

 ArrayView(string valueName, uint textSize=16)
  : valueName(valueName), textSize(textSize) {
  if(arguments() && startsWith(arguments()[0],"server-")) {
   if(!existsFile(arguments()[0]) || File(arguments()[0]).modifiedTime() < realTime()-60*60e9) {
    Stream stdout;
    Time time;
    execute("/usr/bin/ssh",{arguments()[0],"qstat","-u", user(), "-s","r"}, true, currentWorkingDirectory(), &stdout);
    log(time);
    auto running = stdout.readUpTo(1<<17);
    assert_(running.size < running.capacity, running.size);
    writeFile(arguments()[0], running);
   }
   TextData s = readFile(arguments()[0]);
   assert_(s);
   s.line(); s.line();
   for(;;) {
    s.whileAny(' ');
    string id = s.whileInteger();
    assert_(id);
    s.whileAny(' '); s.decimal(); // Priority
    s.whileAny(' '); s.whileNot(' '); // Short name
    s.whileAny(' '); s.whileNot(' '); // User name
    s.whileAny(' '); s.whileNot(' '); // Status
    Date date = parseDate(s); // Start date
    running.insert(copyRef(id), currentTime()-date);
    if(!s) break;
    s.skip('\n');
   }
   log(running);
  }
  Folder results ("."_);
  for(string name: results.list(Files)) {
   if(!name.contains('.')) continue;
   auto file = readFile(name);
   if(!file) continue;
   Dict configuration = parseDict(section(name,'.',0,-2));
   if(!configuration.contains("Pattern")) continue;
   float value = 0;
   if(endsWith(name,".result") || endsWith(name,".working")) {
    map<string, array<float>> dataSets;
    TextData s (file);
    s.until('\n'); // First line: constant results
    buffer<string> names = split(s.until('\n'),", "); // Second line: Headers
    for(string name: names) dataSets.insert(name);
    while(s) {
     for(size_t i = 0; s && !s.match('\n'); i++) {
      string d = s.whileDecimal();
      if(!d) goto break2;
      //assert_(d, s.slice(s.index-16,16),"|", s.slice(s.index));
      float decimal = parseDecimal(d);
      assert_(isNumber(decimal), s.slice(s.index-16,16),"|", s.slice(s.index));
      if(!(i < dataSets.values.size)) break;
      assert_(i < dataSets.values.size, i, dataSets.keys);
      dataSets.values[i].append( decimal );
      s.whileAny(' ');
     }
    }
    break2:;
    if(!dataSets.contains("Stress (Pa)")) continue;
    value = ::max(dataSets.at("Stress (Pa)")) / 1e6;
    continue; // Time only
   } else {
    TextData suffix {section(name,'.',-2,-1)};
    suffix.skip('e');
    string id = suffix.whileInteger();
    assert_(!suffix, suffix);
    TextData s (file);
    string lastTime;
    while(s) {
     string number = s.whileDecimal(); // Simulation time
     if(number && number!="0"_) {
      s.skip(' ');
      number = s.whileDecimal(); // Real time
      assert_(number);
      lastTime = number;
     }
     s.line();
    }
    if(!lastTime) continue;
    value = parseDecimal(lastTime);
    ids.insert(parseDict(str(configuration)), copyRef(id));
   }
   assert_(value>0, name);
   if(points.contains(configuration)) { log("Duplicate configuration", configuration); continue; }
   points.insert(move(configuration), value);
  }
  assert_(points);
  min = ::min(points.values);
  max = ::max(points.values);
  for(string id: running.keys) if(!ids.values.contains(id)) {
   //log("Missing output for ", id);
   points.insert(ids)
  }
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
    bool done = false;
    for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
     if(done) {
      for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
       log(coordinates);
      }
     }
     float value = points.at(coordinates);
     float v = max>min ? (value-min)/(max-min) : 0;
     assert_(v>=0 && v<=1, v, value, min, max);
     vec2 cellOrigin (vec2(levelCount().yx()+int2(1))*headerCellSize+origin*cellSize);
     graphics.fills.append(cellOrigin, cellSize, bgr3f(0,1-v,v));
     targets.append(Rect{cellOrigin, cellOrigin+cellSize}, coordinates);
     float realValue = value; //abs(value); // Values where maximum is best have been negated
     String text = str(int(round(realValue))); //point.isInteger?dec(realValue):ftoa(realValue);
     //if(value==max) text = bold(text);
     if(running.contains(ids.at(coordinates))) text = bold(text);
     graphics.graphics.insert(cellOrigin, Text(text, textSize, 0, 1, 0,
                                               "DejaVuSans", true, 1, 0).graphics(cellSize));
     done=true;
     //break;
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
  for(const auto& coordinate: coordinates(points)) {
   if(coordinate.value.size==1) fixed.append(coordinate.key+": "_+str(coordinate.value)+"\n"_);
   /*else if(!dimensions[0].contains(coordinate.key) && !dimensions[1].contains(coordinate.key))
    log("Hidden dimension", coordinate.key);*/
  }
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
  targets.clear();
  Dict filterX, filterY; renderCell(graphics, cellSize, 0, 0, filterX, filterY);

  // Headers (and lines over fills)
  for(uint axis: range(2)) { Dict filter; renderHeader(graphics, size, cellSize, axis, 0, filter); }
  return graphics;
 }

 bool mouseEvent(vec2 cursor, vec2, Event event, Button button, Widget*&) override {
  if(event == Press && button == LeftButton) {
   for(auto& target: targets) {
    if(target.rect.contains(cursor)) {
     log(ids.at(target.key));
     log(readFile(replace(str(target.key),":","=")+".e"+str(ids.at(target.key))));
     return true;
    }
   }
  }
  return false;
 }
};

struct Review {
 ArrayView view {/*"Stress (MPa)"*/"Time (s)"};
 unique<Window> window = ::window(&view, int2(0, 720));
} app;
