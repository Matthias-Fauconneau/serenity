#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"
#include "variant.h"
#include "xml.h"
#include "plot.h"
#include "layout.h"

#define RENAME 0
#if RENAME
struct Rename {
 Rename() {
  Folder results {"."_};
  for(string name: results.list(Files)) {
   if(!endsWith(name,".result") && !endsWith(name,".working")) {
    if(endsWith(name,".failed")||endsWith(name,".working")) {
     log("-", name);
     //remove(name);
    } //else log("Unknown file", name);
    continue;
   }
   Dict parameters = parseDict(section(name,'.',0,-2));
   //parameters.keys.replace("subStepCount"__, "Substep count"__);
   String newName = str(parameters)+"."+section(name,'.',-2,-1);
   newName = replace(newName, ".working.result", ".working");
   if(name != newName) {
    log(name, newName);
    rename(name, newName, results);
   }
  }
 }
} app;
#endif

struct SGEJob { Dict dict; String id; float elapsed; };
String str(const SGEJob& o) { return str(o.dict, o.id, o.elapsed); }
//bool operator==(const SGEJob& o, string id) { return o.id == id; }
bool operator==(const SGEJob& o, const Dict& dict) { return o.dict == dict; }

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, String> ids; // Job IDs
 map<Dict, String> states; // Job states
 array<SGEJob> running;
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
 size_t index = 0;
 function<void(const Dict&)> hover;

 void fetchRunning(int time) {
  if(arguments() && startsWith(arguments()[0],"server-")) {
   if(!existsFile(arguments()[0], ".cache"_) ||
      File(arguments()[0], ".cache"_).modifiedTime() < realTime()-time*60e9) {
    Stream stdout;
    Time time;
    int pid = execute("/usr/bin/ssh",ref<string>{arguments()[0],"qstat"_,"-u"_,user(),"-s"_,"r"_,"-xml"_}, false, currentWorkingDirectory(), &stdout);
    array<byte> status;
    for(;;) {
     auto packet = stdout.readUpTo(1<<16);
     status.append(packet);
     if(!(packet || isRunning(pid))) break;
    }
    log(time);
    writeFile(arguments()[0], status, ".cache"_, true);
   }
   auto document = readFile(arguments()[0], ".cache"_);
   Element root = parseXML(document);
   for(const Element& job: root("job_info")("queue_info").children) {
    auto dict = parseDict(job("JB_name").content);
    running.append(SGEJob{move(dict), copyRef(job("JB_job_number").content),
                          float(currentTime()-parseDate(job("JAT_start_time").content))});
   }
  }
 }

 ArrayView(string valueName, uint textSize=16)
  : valueName(valueName), textSize(textSize) {
  fetchRunning(30);
  load();
 }
 void load() {
  valueName = ref<string>{"Stress (MPa)","Time (s)"}[index];
  points.clear();
  ids.clear();
  states.clear();
  Folder results ("."_);
  for(string name: results.list(Files)) {
   if(!name.contains('.')) continue;
   auto file = readFile(name);
   if(!file) continue;
   Dict configuration = parseDict(section(name,'.',0,-2));
   if(!configuration.contains("Pattern")) continue;
   if(!existsFile(name,".cache"_)) { // FIXME: check mtime
    String data;
    if(index == 0 && (endsWith(name,".result") || endsWith(name,".working"))) {
     map<string, array<float>> dataSets;
     // TODO: optimize
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
     data = str(::max(dataSets.at("Stress (Pa)")) / 1e6);
    } else if(!(endsWith(name,".result") || endsWith(name,".working"))) {
     TextData suffix {section(name,'.',-2,-1)};
     suffix.skip('e');
     string id = suffix.whileInteger();
     assert_(!suffix, suffix);
     TextData s (file);
     string lastTime;
     string state;
     while(s) {
      if(s.match("pour")) state = "pour"_;
      if(s.match("pack")) state = "pack"_;
      if(s.match("load")) state = "load"_;
      string number = s.whileDecimal(); // Simulation time
      if(number && number!="0"_) {
       s.skip(' ');
       number = s.whileDecimal(); // Real time
       assert_(number);
       lastTime = number;
      }
      s.line();
     }
     data = str(lastTime?parseDecimal(lastTime)/60/60 : 0, id, state);
    }
    log(name, data);
    writeFile(name, data, ".cache"_);
   }
   TextData s (readFile(name, ".cache"_));
   float value = s.decimal();
   if(!(endsWith(name,".result") || endsWith(name,".working"))) {
    s.skip(' ');
    ids.insert(copy(configuration), str(s.integer()));
    s.skip(' ');
    states.insert(copy(configuration), copyRef(s.word()));
    if(index == 0 && points.contains(configuration)) continue;
   } else if(index==1) continue;
   //assert_(value>0, name);
   if(points.contains(configuration)) {
    if(points.at(configuration)!=0) { log("Duplicate configuration", configuration); continue; }
    else points.at(configuration) = value; // Replace 0 from log
   } else
    points.insert(move(configuration), value);
  }
  if(index==1) for(const SGEJob& job: running) if(!points.contains(job.dict)) {
   //log("Missing output for ", id);
   assert_(job.dict);
   points.insert(copy(job.dict), job.elapsed/60/60);
  }
  assert_(points);
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
     bgr3f color (1,1,1);
     if(states.contains(coordinates)) {
      string state = states.at(coordinates);
      if(state=="pack") color = bgr3f(0,0,1);
      else if(state=="load") color = bgr3f(0,1,0);
     }
     float realValue = value; //abs(value); // Values where maximum is best have been negated
     if(index==0 && realValue) color = bgr3f(0,1-v,v);
     graphics.fills.append(cellOrigin, cellSize, color);
     targets.append(Rect{cellOrigin, cellOrigin+cellSize}, coordinates);
     String text = str(int(round(realValue))); //point.isInteger?dec(realValue):ftoa(realValue);
     //if(value==max) text = bold(text);
     if(running.contains(coordinates)) text = bold(text);
     if(realValue) graphics.graphics.insert(cellOrigin, Text(text, textSize, 0, 1, 0,
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
  if(button == WheelUp || button == WheelDown) {
   index = (++index)%2;
   load();
   return true;
  }
  if(event == Press && button == LeftButton) {
   for(auto& target: targets) {
    if(target.rect.contains(cursor)) {
     log(str(target.key));
     if(states.contains(target.key)) log(states.at(target.key));
     if(ids.contains(target.key)) {
      if(running.contains(target.key)) log(running[running.indexOf(target.key)]);
      log(ids.at(target.key)); //qdel -f 669165 > /dev/null &
      String name = replace(str(target.key),":","=")+".e"+str(ids.at(target.key));
      if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));
     }
     for(string name: Folder(".").list(Files)) if(startsWith(str(target.key), name)) log(name);
     /*{String name = str(target.key)+".result";
      if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));}
     {String name = str(target.key)+".working";
      if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));}*/
     return true;
    }
   }
  }
  if(event == Motion) {
   for(auto& target: targets) {
    if(target.rect.contains(cursor)) {
     hover(target.key);
    }
   }
  }
  return false;
 }
};

struct Review {
 ArrayView view {/*"Stress (MPa)"*/"Time (s)"};
 Plot plot;
 VBox layout {{&view, &plot}};
 unique<Window> window = ::window(&layout, int2(0, 1050));
 Review() {
  window->actions[Return] = [this](){ view.fetchRunning(0); window->render(); };
  view.hover = [this](const Dict& point) {
   Dict filter = copy(point);
   filter.remove("Pressure");
   filter.remove("Pattern");
   plot.xlabel = "Pressure (N)"__;
   plot.ylabel = "Stress (Pa)"__;
   plot.dataSets.clear();
   array<String> fixed;
   for(const auto& coordinate: view.coordinates(view.points))
    if(coordinate.value.size==1) fixed.append(copyRef(coordinate.key));
   auto patterns = view.coordinates("Pattern", filter);
   Dict parameters = copy(filter);
   for(const Variant& pattern: patterns) {
    parameters[copyRef("Pattern"_)] = copy(pattern);
    auto pressures = view.coordinates("Pressure", filter);
    for(const Variant& pressure: pressures) {
     Dict shortSet = copy(parameters);
     for(string dimension: fixed) shortSet.remove(dimension);
     auto& dataSet = plot.dataSets[str(shortSet," "_)];
     Dict key = copy(parameters);
     key.insertSorted(copyRef("Pressure"_), copy(pressure));
     if(view.points.contains(key)) {
      float maxStress = view.points.at(key);
      dataSet.insertSorted(float(pressure), maxStress);
     }
    }
   }
   window->render();
  };
 }
}
#if !RENAME
app
#endif
;
