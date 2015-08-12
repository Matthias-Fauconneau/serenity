#include "thread.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"
#include "variant.h"
#include "xml.h"
#include "plot.h"
#include "layout.h"
#include "render.h"
#include "pdf.h"
#include "snapshot-view.h"
#include "sge.h"
#include <unistd.h>

constexpr size_t medianWindowRadius = 12;
buffer<float> medianFilter(ref<float> source, size_t W=medianWindowRadius) {
 assert_(source.size > W+1+W);
 buffer<float> target(source.size-2*W);
 //target.slice(0, W).copy(source.slice(0, W));
 buffer<float> window(W+1+W);
 for(size_t i: range(W, source.size-W)) {
  window.copy(source.slice(i-W, W+1+W));
  target[i-W] = ::median(window); // Quickselect median mutates buffer
 }
 //target.slice(target.size-W).copy(source.slice(source.size-W));
 return target;
}

inline Fit totalLeastSquare(ref<float> X, ref<float> Y) {
 assert_(X.size == Y.size);
 float mx = mean(X), my = mean(Y);
 size_t N = X.size;
 if(N<=1) return {0,0};
 assert_(N>1);
 float sxx=0; for(float x: X) sxx += sq(x-mx); sxx /= (N-1);
 float sxy=0; for(size_t i: range(N)) sxy += (X[i]-mx)*(Y[i]-my); sxy /= (N-1);
 float syy=0; for(float y: Y) syy += sq(y-my); syy /= (N-1);
 float a = (syy - sxx + sqrt(sq(syy-sxx)+4*sq(sxy))) / (2*sxy);
 float b = my - a*mx;
 return {a, b};
}

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, String> ids; // Job IDs
 map<Dict, String> states; // Job states
 array<SGEJob> jobs;
 float minDisplacement = inf, commonDisplacement = 0;
 float min = inf, max = -inf;
 uint textSize;
 vec2 headerCellSize = vec2(80*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 array<string> dimensions[2];
 struct Target { Rect rect; Dict tableCoordinates; const Dict& key; };
 array<Target> targets;
 size_t index = 0;
 function<void(const Dict&)> hover, press;
 function<void(string)> output;
 Folder cache {".cache", currentWorkingDirectory(), true};
 bool useMedianFilter = true;
 enum { StressPressure, PressureStress, Deviatoric }; size_t plotIndex = PressureStress;
 bool deviatoric = false;

 ArrayView(string valueName, uint textSize=16) : valueName(valueName), textSize(textSize) {
  jobs = qstat(30);
  load();
 }

 // Prepends sort key
 void apply(Dict& dict) {
  if(dict.contains("Pattern"_)) { // Sort key
   int index = ref<string>{"none"_,"helix","cross","loop"}.indexOf(dict.at("Pattern"));
   assert_(index >= 0 && index < 4);
   dict.at("Pattern"_) = String( (char)index + (string)dict.at("Pattern"));
  }
 }

 // Applies sort key on load
 Dict parseDict(string id) {
  Dict configuration = ::parseDict(id);
  apply(configuration);
  return move(configuration);
 }
 array<SGEJob> qstat(int time) {
  auto jobs  = ::qstat(time);
  for(auto& job: jobs) apply(job.dict);
  return jobs;
 }

 // Strips sort keys
 Dict stripSortKeys(const Dict& o) {
  Dict dict = copy(o);
  for(auto& value: dict.values) if(value.type==Variant::Data && value.data && value.data[0] < 16) {
   value.data = copyRef(value.data.slice(1)); // Strips sort key
   assert_(value.data && value.data[0] >= 16);
  }
  return dict;
 }

 void load(int time=60) {
  valueName = ref<string>{"Peak Stress (Pa)", "Time (s)", "Wire density (%)", "Displacement (%)"}[index];
  points.clear();
  ids.clear();
  states.clear();
  Folder results ("."_);
  auto list = results.list(Files);
  for(string fileName: list) {
   if(!fileName.contains('.')) continue;
   string id = section(fileName,'.',0,-2);
   string suffix = section(fileName,'.',-2,-1);
   if(!startsWith(suffix,"o") && suffix!="result" && suffix!="working" && suffix!="failed") continue;
   Dict configuration = parseDict(id);
   if(points.contains(configuration)) continue;
   array<char> data;
   if(1 && existsFile(id,cache) && File(id, cache).modifiedTime() >= realTime()-time*60e9)
    data = readFile(id, cache); // FIXME: do not reload old unchanged files
   if(!data) {
    String resultName;
    if(existsFile(id+".failed")) resultName = id+".failed";
    if(existsFile(id+".working")) resultName = id+".working";
    if(existsFile(id+".result")) resultName = id+".result";
    if(resultName) {
     map<string, array<float>> dataSets;
     TextData s (readFile(resultName));
     if(s) {
      string resultLine = s.line();
      string headerLine;
      if(resultLine.contains('=')) headerLine = s.line();
      else { // FIXME: old bad version does not write the result line
       headerLine = resultLine;
       resultLine = ""_;
      }
      Dict results = parseDict(resultLine);
      if(results.contains("Wire density (%)"_))
       data.append((string)results.at("Wire density (%)"_));
      else
       data.append("0"_);
      buffer<string> names = split(headerLine,", "); // Second line: Headers
      for(string name: names) dataSets.insert(name);
      //assert_(s, resultName, s);
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
      //if(dataSets.contains("Stress (Pa)")) //continue;
      //assert_(dataSets.at("Stress (Pa)"_), dataSets);
      if(dataSets.at("Stress (Pa)"_)) {
       auto& stress = dataSets.at("Stress (Pa)"_);
       if(stress.size > 2*medianWindowRadius+1) data.append(" "_+str(::max(medianFilter(stress))));
       else data.append(" 0"_);
      } else data.append(" 0"_);
     } else data.append("0 0"_);
    } else data.append("0 0"_);
    string logName;
    for(string name: list) {
     string jobID;
     {TextData s (name);
      if(!s.match(id)) continue;
      if(!s.match(".o")) continue;
      jobID = s.whileInteger();
      assert_(!s);
     }
     if(logName) error("Duplicate", logName, name);
     TextData s (readFile(name));
     string time;
     string state;
     string displacement;
     for(;s; s.line()) {
      if(s.match("pour")) state = "pour"_;
      else if(s.match("pack")) state = "pack"_;
      else if(s.match("load")) state = "load"_;
      else continue;
      s.skip(' '); s.whileDecimal(); // Pressure
      s.skip(' '); s.whileDecimal(); // dt
      s.skip(' '); s.whileDecimal(); // grain count
      s.skip(' ');
      time = s.whileDecimal(); s.skip("s "); // Simulation time
      s.whileDecimal(); s.until(' '); // Energy
      if(state == "load"_) {
       displacement = s.whileDecimal();
       assert_(displacement, s);
      }
     }
     assert_(jobID);
     data.append(" "_+str(time?:"0"_, jobID, state, displacement?:"0"_)); // /60/60
     logName=name; break;
    }
    //assert_(logName);
    assert_(data, index, fileName, id, resultName, logName);
    log(id, data);
    writeFile(id, data, cache, true);
   }
   TextData s (data);
   float wireDensity = s.decimal();
   s.skip(' ');
   float stress = s.decimal();
   float time=0, displacement=0;
   if(s) {
    s.skip(' ');
    time = s.decimal();
    s.skip(' ');
    ids.insert(copy(configuration), str(s.integer()));
    s.skip(' ');
    states.insert(copy(configuration), copyRef(s.word()));
    s.skip(' ');
    displacement = s.decimal();
    minDisplacement = ::min(minDisplacement, displacement);
    if(displacement>=12.4) states.at(configuration) = "done"__;
   }
   /*if(points.contains(configuration)) {
    if(1 || points.at(configuration)!=0) { log("Duplicate configuration", configuration); continue; }
    else points.at(configuration) = value; // Replace 0 from log
   } else*/
   points.insert(move(configuration), ref<float>{stress,time,wireDensity,displacement}[index]);
  }
  commonDisplacement = minDisplacement; // Used on next load

  for(const SGEJob& job: jobs) if(!points.contains(job.dict)) {
   //log("Missing output for ", id);
   assert_(job.dict);
   if(index==1) points.insert(copy(job.dict), job.elapsed/60/60);
   else points.insert(copy(job.dict), 0);
  }

  if(points) {
   min = ::min(points.values);
   max = ::max(points.values);
  }
  dimensions[0] = copyRef(ref<string>{"TimeStep"_, "Radius"_,"Pressure"_,"Seed"_});
  //dimensions[1] = copyRef(ref<string>{"Wire"_,"Pattern"_,"Angle"_}); // FIXME
  dimensions[1] = copyRef(ref<string>{"Wire"_,"Angle"_,"Pattern"_}); // FIXME
  for(auto& dimensions: this->dimensions) {
   dimensions.filter([this](const string dimension) {
    for(const Dict& coordinates: points.keys) if(coordinates.keys.contains(dimension)) return false;
    return true; // Filters unknown dimension
   });
  }

  //array<char> zombies;
  for(const SGEJob& job: jobs)  if(states.contains(job.dict) && states.at(job.dict) == "done") {
   logInfo(job.dict);
   //zombies.append(job.id+" ");
  }
  //log(zombies);

  size_t failureCount = 0, fileCount = 0;
  for(const Dict& key: points.keys) {
   if(states.contains(key) && states.at(key)!="done"_ && !jobs.contains(key)) {
    String id = str(stripSortKeys(key));
    for(string name: Folder(".").list(Files)) if(startsWith(name, id)) {
     log(name);
     fileCount++;
    }
    failureCount++;
   }
  }
  if(failureCount) log("Failed configuration", failureCount, "files", fileCount);
 }

 void removeFailed() {
  size_t failureCount = 0, fileCount = 0;
  for(const Dict& key: points.keys) {
   if(states.contains(key) && states.at(key)!="done"_ && !jobs.contains(key)) {
    String id = str(stripSortKeys(key));
    for(string name: Folder(".").list(Files)) if(startsWith(name, id)) {
     log(name);
     fileCount++;
     remove(name);
    }
    failureCount++;
   }
  }
  //log(failures.size);
  log(failureCount, fileCount);
 }

 void logInfo(const Dict& key) {
  String id = str(stripSortKeys(key));
  //log(id);
  //if(states.contains(key)) log(states.at(key));
  //auto data = readFile(id, cache);
  array<char> data;
  if(ids.contains(key)) {
   //if(jobs.contains(key)) log(jobs[jobs.indexOf(key)]);
   String name = id+".o"+str(ids.at(key));
   if(existsFile(name)) data.append(/*"\n"+*/trim(section(readFile(name),'\n',-10,-1)));
   else log("Missing output", name);
  }
  if(output) output(data);
  //for(string name: Folder(".").list(Files)) if(startsWith(name, str(target.coordinates)))
  /*{String name = str(key)+".result";
  if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));}
 {String name = str(key)+".working";
  if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));}*/
 }

 /// Returns coordinates along \a dimension occuring in points matching \a filter
 array<Variant> coordinates(string dimension, const Dict& filter) const {
  array<Variant> allCoordinates;
  for(const Dict& coordinates: points.keys) if(coordinates.includes(filter)) {
   //assert_(coordinates.contains(dimension), coordinates, dimension);
   Variant value;
   if(coordinates.contains(dimension)) value = copy(coordinates.at(dimension));
   if(!allCoordinates.contains(value)) allCoordinates.insertSorted(move(value));
  }
  return allCoordinates;
 }
 /// Returns coordinates occuring in \a points
 map<String, array<Variant> > coordinates(ref<Dict> keys) const {
  map<String, array<Variant>> allCoordinates;
  for(const Dict& coordinates: keys) for(const auto coordinate: coordinates)
   if(!allCoordinates[copy(coordinate.key)].contains(coordinate.value)) allCoordinates.at(coordinate.key).insertSorted(copy(coordinate.value));
  return allCoordinates;
 }
 /// Returns number of cells for the given \a axis, \a level and \a coordinates
 int cellCount(uint axis, uint level, Dict& filter) const {
  if(level == dimensions[axis].size) return 1;
  string dimension = dimensions[axis][level];
  int cellCount = 0;
  assert_(!filter.contains(dimension));
  //assert_(coordinates(dimension, filter), dimension, filter, coordinates(dimension, {}), points.keys);
  for(const Variant& coordinate: coordinates(dimension, filter)) {
   filter[copyRef(dimension)] = copy(coordinate);
   cellCount += this->cellCount(axis, level+1, filter);
  }
  if(filter.contains(dimension)) filter.remove(dimension);
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
   //assert_(coordinate);
   filter[copyRef(dimension)] = copy(coordinate);
   uint childCellCount = renderHeader(graphics, viewSize, contentCellSize, axis, level+1, filter, offset+cellCount);
   vec2 headerOrigin (dimensions[!axis].size+1, level);
   vec2 cellCoordinates (offset+cellCount, 0);
   vec2 size (childCellCount, 1);
   if(axis) headerOrigin=headerOrigin.yx(), cellCoordinates=cellCoordinates.yx(), size=size.yx();
   vec2 cellSize = axis ? vec2(headerCellSize.x, contentCellSize.y) : vec2(contentCellSize.x, headerCellSize.y);
   const vec2 origin = headerOrigin*headerCellSize + cellCoordinates*cellSize;
   if(level<dimensions[axis].size-1) {
    int width = dimensions[axis].size-1-level;
    if(!axis) graphics.fills.append(origin+vec2(-width/2,0), vec2((width+1)/2, viewSize.y));
    if(axis) graphics.fills.append(origin+vec2(0,-width/2), vec2(viewSize.x,(width+1)/2));
   }
   assert_(isNumber(origin), origin, headerOrigin, headerCellSize, cellCoordinates, cellSize, contentCellSize);
   array<char> label = str(coordinate);
   if(label && label[0] < 16) label.removeAt(0); // Removes sort key
   graphics.graphics.insertMulti(origin, Text(label, textSize, 0, 1, 0,
                                         "DejaVuSans", true, 1, 0).graphics(vec2(size*cellSize)));
   cellCount += childCellCount;
  }
  if(filter.contains(dimension)) filter.remove(dimension);
  return cellCount;
 }

 int renderCell(Graphics& graphics, vec2 cellSize, uint axis, uint level, Dict& filterX, Dict& filterY, vec2 origin=0) {
  if(level==dimensions[axis].size) {
   if(axis==0) { renderCell(graphics, cellSize, 1, 0, filterX, filterY, origin); } // Descends dimensions tree on other array axis
   else { // Renders cell
    const Dict* best = 0;
    float bestValue = 0;
    bool running = false, pending = false, hasSnapshotSignalFile = false;
    for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
     if(existsFile(str(stripSortKeys(coordinates)))) hasSnapshotSignalFile = true;
     for(const auto& job: jobs) if(job.dict.includes(coordinates)) {
      if(job.state=="running") running = true;
      if(job.state=="pending") pending = true;
     }
     float value = points.at(coordinates);
     if(value >= bestValue) {
      bestValue = value;
      best = &coordinates;
     }
    }
    if(best) {
     const Dict& coordinates = *best;
     float value = bestValue;
     vec2 cellOrigin (vec2(levelCount().yx()+int2(1))*headerCellSize+origin*cellSize);

     bgr3f color (1,1,1);
     if(states.contains(coordinates)) {
      string state = states.at(coordinates);
      /**/  if(state=="pour") color = bgr3f(1./2);
      else if(state=="pack") color = bgr3f(3./4);
      else if(state=="load") color = bgr3f(7./8);
      else if(state=="done") color = bgr3f(1);
      else if(state) log("Unknown state", state);
      if(!running && state!="done") {color.r=1; color.b=color.g=1./2;}
     }
     //if(index==0 && value) color = bgr3f(0,1-v,v);
     graphics.fills.append(cellOrigin, cellSize, color);

     Dict tableCoordinates = copy(filterX); tableCoordinates.append(copy(filterY));
     targets.append(Rect{cellOrigin, cellOrigin+cellSize}, move(tableCoordinates), coordinates);
     if(index==0) value /= 1e6; // MPa
     String text = str(int(round(value))); //point.isInteger?dec(value):ftoa(value);
     //if(value==max) text = bold(text);
     if(pending) text = italic(text);
     if(running) text = bold(text);
     bgr3f textColor = 0;
     if(running && !hasSnapshotSignalFile) textColor = red; // Old version
     /*if(value)*/ graphics.graphics.insertMulti(cellOrigin, Text(text, textSize, textColor, 1, 0,
                                               "DejaVuSans", true, 1, 0).graphics(cellSize));
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
  if(filter.contains(dimension)) filter.remove(dimension);
  return offset;
 }

 shared<Graphics> graphics(vec2 size) override {
  shared<Graphics> graphics;

  if(0) {// Fixed coordinates in unused top-left corner
   array<char> fixed;
   for(const auto& coordinate: coordinates(points.keys)) {
    if(coordinate.value.size==1) fixed.append(coordinate.key+": "_+str(coordinate.value)+"\n"_);
    /*else if(!dimensions[0].contains(coordinate.key) && !dimensions[1].contains(coordinate.key))
    log("Hidden dimension", coordinate.key);*/
   }
   graphics->graphics.insert(vec2(0,0), Text(fixed, textSize).graphics(
                            vec2(dimensions[1].size,dimensions[0].size)*headerCellSize));
  }

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
  vec2 cellSize = (size - vec2(levelCount().yx()+int2(1))*headerCellSize ) / vec2(cellCount());
  assert_(isNumber(cellSize), cellSize, cellCount());
  targets.clear();
  Dict filterX, filterY; renderCell(graphics, cellSize, 0, 0, filterX, filterY);

  // Headers (and lines over fills)
  for(uint axis: range(2)) { Dict filter; renderHeader(graphics, size, cellSize, axis, 0, filter); }
  return graphics;
 }

 bool mouseEvent(vec2 cursor, vec2, Event event, Button button, Widget*&) override {
  if(event == Press && (button == WheelUp || button == WheelDown)) {
   index=(index+4+(button==WheelUp?1:-1))%4;
   load();
   return true;
  }
  if(event == Press && button == LeftButton) {
   for(const auto& target: targets) {
    if(target.rect.contains(cursor)) {
     logInfo(target.key);
     press(target.key);
     return true;
    }
   }
   press({});
  }
  if(event == Motion) {
   for(auto& target: targets) {
    if(target.rect.contains(cursor)) {
     hover(target.tableCoordinates);
    }
   }
  }
  return false;
 }
};

struct Review {
 ArrayView view {/*"Stress (MPa)"*/"Time (s)"};
 Plot plot;
 SnapshotView snapshotView;
 Plot stressStrain, volumeStrain;
 Text output;
 HBox detail {{&plot, &snapshotView}};
 HBox plots {{&stressStrain, &volumeStrain}};
 VBox layout {{&view, &detail, &plots, &output}};
 unique<Window> window = ::window(&layout, int2(0, 0), mainThread, true);
 Review() {
  window->actions[Key('m')] = [this](){
   view.useMedianFilter=!view.useMedianFilter;
   window->render();
  };
  window->actions[Key('c')] = [this](){
   view.plotIndex=(view.plotIndex+1)%3;
   window->render();
  };
  window->actions[Key('d')] = [this](){
   view.deviatoric = !view.deviatoric;
   window->render();
  };
  window->actions[Key('x')] = [this](){
   view.dimensions[0] = array<string>(view.dimensions[0].slice(1) + view.dimensions[0][0]);
   window->render();
  };
  window->actions[Key('y')] = [this](){
   view.dimensions[1] = array<string>(view.dimensions[1].slice(1) + view.dimensions[1][0]);
   window->render();
  };
  window->actions[Delete] = [this]() { view.removeFailed(); view.load(10); window->render(); };
  window->actions[Return] = [this](){
   window->setTitle("Refreshing");
   view.jobs = view.qstat(0); view.load(0); window->render();
   window->setTitle(str(view.jobs.size));
  };
  window->actions[Space] = [this](){
   //remove("plot.pdf"_, home());
   static constexpr float inchMM = 25.4, inchPx = 90;
   const vec2 pageSize (210/*mm*/ * (inchPx/inchMM), 210/*mm*/ * (inchPx/inchMM));
   auto graphics = plot.graphics(pageSize);
   graphics->flatten();
   writeFile("plot.pdf"_, toPDF(pageSize, ref<Graphics>(graphics.pointer, 1), 72 / inchPx /*px/inch*/), home(), true);
   //encodePNG(render(int2(1050), plot.graphics(vec2(1050))));
  };
  view.hover = [this](const Dict& point) {
   if(view.index!=0) return;
   Dict filter = copy(point);
   if(filter.contains("Pressure"_)) filter.remove("Pressure");
   if(filter.contains(view.dimensions[0].last())) filter.remove(view.dimensions[0].last());
   if(filter.contains(view.dimensions[1].last())) filter.remove(view.dimensions[1].last());
   plot.plotPoints = true, plot.plotLines = false;
   plot.min = 0; plot.max = 0;
   if(view.plotIndex==ArrayView::PressureStress) {
    plot.ylabel = "Pressure (Pa)"__;
    plot.xlabel = "Stress (Pa)"__;
    plot.plotBandsX = true;
    plot.plotBandsY = false;
    plot.plotCircles = -1;
    plot.max = view.max;
   } else if(view.plotIndex==ArrayView::StressPressure) {
    plot.ylabel = "Stress (Pa)"__;
    plot.xlabel = "Pressure (Pa)"__;
    plot.plotBandsY = true;
    plot.plotBandsX = false;
    plot.plotCircles = -1;
    plot.min.y = 0, plot.max.y = view.max;
   } else if(view.plotIndex==ArrayView::Deviatoric) {
    plot.ylabel = "Stress + Pressure (Pa)"__;
    plot.xlabel = "Deviatoric Stress (Pa)"__;
    plot.plotBandsX = true;
    plot.plotBandsY = false;
    plot.plotCircles = false;
   }
   plot.dataSets.clear();
   plot.fits.clear();
   array<Dict> points;
   for(const auto& point: view.points) {
    if(point.key.includesPassMissing(filter)) {
     points.append(copy(point.key));
    }
   }
   array<String> fixed;
   for(const auto& coordinate: view.coordinates(points))
    if(coordinate.value.size==1) fixed.append(copyRef(coordinate.key));
   //map<String, map<float, float>> deviatorics;
   for(const auto& point: points) {
    Dict shortSet = copy(point);
    for(string dimension: fixed) if(shortSet.contains(dimension)) shortSet.remove(dimension);
    if(shortSet.contains("Pressure")) shortSet.remove("Pressure");
    if(shortSet.contains(view.dimensions[0].last())) shortSet.remove(view.dimensions[0].last());
    if(view.dimensions[0].last()=="Radius"_)
     if(shortSet.contains("Height")) shortSet.remove("Height");
    String id = str(shortSet.values," "_,""_);
    auto& dataSet = plot.dataSets[::copy(id)];
    float maxStress = view.points.at(point);
    if(maxStress) {
     float s1 = maxStress, s3 = float(point.at("Pressure"));
     if(view.plotIndex==ArrayView::Deviatoric) { // TODO: Regression
      dataSet.insertSortedMulti((s1-s3)/2, (s1+s3)/2); // ?
     } else if(view.plotIndex==ArrayView::PressureStress) {
       // Pressure (Shear Stress) vs Normal Stress (Peak)) for Mohr's circles
      dataSet.insertSortedMulti(maxStress, float(point.at("Pressure")));
      //deviatorics[copy(id)].insertSortedMulti((s1-s3)/2, (s1+s3)/2); // ?
     }
     else if(view.plotIndex==ArrayView::StressPressure) {
       // Experiment (Peak Stress vs Pressure)
      dataSet.insertSortedMulti(float(point.at("Pressure")), maxStress);
     }
    }
   }
   if(view.plotIndex==ArrayView::Deviatoric) {
    for(auto entry: plot.dataSets) {
     auto f = totalLeastSquare(entry.value.keys, entry.value.values);
     plot.fits[copy(entry.key)].append(f);
    }
   }
   if(view.plotIndex==ArrayView::PressureStress && plot.dataSets) {
    const size_t N = 16;
    /*buffer<map<NaturalString, map<float, float>>> tangents (N); tangents.clear();
    buffer<array<Fit>> fits (N); fits.clear();
    size_t bestIndex = 0;*/
    for(auto entry: plot.dataSets) {

     map<float, float> sortY;
     for(auto p: entry.value) sortY.insertSortedMulti(p.value, p.key);

     //const auto& X = entry.value.keys, &Y = entry.value.values;
     const auto& Y = sortY.keys, &X = sortY.values;
     buffer<float> x (X.size), y (Y.size);

     float bestSSR = inf; //φ = 0,
     Fit bestFit;
     for(size_t angleIndex: range(N)) {
      float φ = PI/2*angleIndex/N;
      for(size_t i: range(X.size)) { x[i] = X[i] - Y[i]*sin(φ); y[i] = Y[i]*cos(φ); }
      auto f = totalLeastSquare(x, y);
      float a=f.a, b=f.b;
      float SSR = 0;
      //float lastY = 0;
      for(size_t i: range(X.size)) {
       //if(y[i] == lastY) continue; lastY=y[i];
       /*if(0) {
        float xi = x[i] + a*(y[i]-(a*x[i]+b));
        SSR += sq(y[i]-(a*xi+b)) + sq(x[i]-xi);
       } else*/ { // Distance to circle tangent point
        float xi = (X[i] - a*b) / (a*a + 1);
        float yi = a*xi + b;
        float R = Y[i];
        float r = sqrt(sq(xi-X[i])+sq(yi));
        float d = R - r;
        //assert_(r > 0, R, d, r);
        /*float xt = X[i] + R/r*(xi-X[i]);
        float yt = 0 + R/r*(yi-0);
        tangents[angleIndex][copy(entry.key)+" fit"].insertSortedMulti(xi, yi);
        tangents[angleIndex][copy(entry.key)+" tangent"].insertSortedMulti(xt, yt);
        //fits[angleIndex].append({-1/a, Y[i]/a});
        {float a = yi/(xi-X[i]); float b = -a*X[i]; fits[angleIndex].append({a,b});}
        plot.plotCircles = 1;
        plot.plotBandsX = false;*/
        SSR +=  d;
       }
      }
      if(SSR < bestSSR) {
       bestSSR = SSR;
       bestFit = f;
       //bestIndex = angleIndex;
       //φ = a;
      }
     }
     plot.fits[copy(entry.key)].append(bestFit);
     //plot.fits[copy(entry.key)].append(move(fits[bestIndex]));
    }
    //plot.dataSets.append(move(tangents[bestIndex]));
  }
   for(auto& key: plot.dataSets.keys) {
    if(key && key[0] < 16) key = copyRef(key.slice(1)); // Strips sort keys
   }
   window->render();
  };
  view.press = [this](const Dict& point) {
   {
    snapshotView.~SnapshotView();
    stressStrain = Plot();
    volumeStrain = Plot();

    map<string, array<float>> dataSets;
    String resultName;
    String id = str(view.stripSortKeys(point));
    if(existsFile(id+".failed")) resultName = id+".failed";
    if(existsFile(id+".working")) resultName = id+".working";
    if(existsFile(id+".result")) resultName = id+".result";
    if(resultName) {
     TextData s (readFile(resultName));
     if(s) {
      string resultLine = s.line();
      string headerLine;
      if(resultLine.contains('=')) headerLine = s.line();
      else { // FIXME: old bad version does not write the result line
       headerLine = resultLine;
       resultLine = ""_;
      }
      Dict results = parseDict(resultLine);
      buffer<string> names = split(headerLine,", "); // Second line: Headers
      for(string name: names) dataSets.insert(name);
      //assert_(s, resultName, s);
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
      /*if(dataSets.contains("Volumetric Strain (%)") ||
         dataSets.contains("Volumetric Stress (%)"))*/ { // FIXME: wrong name in old version
       auto& plot = volumeStrain;
       plot.plotPoints = false, plot.plotLines = true;
       //plot.min.y = 0, plot.max.y = view.max;
       plot.dataSets.clear();
       plot.xlabel = "Strain (%)"__;
       plot.ylabel = /*dataSets.contains("Volumetric Strain (%)")?*/"Volumetric Strain (%)"__;
       //:"Volumetric Stress (%)"__;
       auto& volume = dataSets.at(plot.ylabel);
       //log(volume);
       assert_(volume);
       //if(plot.ylabel=="Volumetric Stress (%)") for(float& v: volume) v = -v;
       auto& strain = dataSets.at(plot.xlabel);
       assert_(strain.size == volume.size, strain.size, volume.size);
       plot.dataSets.insert(""__, {::copy(strain), ::move(volume)});
      }

      {auto& plot = stressStrain;
       plot.plotPoints = false, plot.plotLines = true;
       plot.min.x = 0, plot.max.x = 100./8;
       plot.dataSets.clear();
       plot.xlabel = "Strain (%)"__;
       plot.ylabel = "Stress (Pa)"__;
       auto& stress = dataSets.at(plot.ylabel);
       auto& strain = dataSets.at(plot.xlabel);
       if(stress.size > 2*medianWindowRadius+1) {
        if(view.useMedianFilter) {
         stress = medianFilter(stress);
         strain = copyRef(strain.slice(medianWindowRadius, strain.size-2*medianWindowRadius));
        }
        if(view.deviatoric) { // Normalized deviator stress
         plot.ylabel = "Normalized Deviatoric Stress"__;
         float pressure = point.at("Pressure"_);
         for(float& s: stress) s = (s-pressure)/(s+pressure);
         plot.min.y = 0; plot.max.y = 1;
        } else {
         plot.min.y = 0; plot.max.y = view.max;
        }
        /*assert_(stress.size <= strain.size);
        strain.size = stress.size;*/
        assert_(stress.size == strain.size);
        plot.dataSets.insert(""__, {::copy(strain), ::move(stress)});
       }
      }
     }
    } else output = Text();
   }

   if(point && existsFile(str(point))) {
    int64 time = realTime();
    File file (str(point));
    file.touch(time);
    usleep(300*1000); // FIXME: signal back
   }
   new (&snapshotView) SnapshotView(str(view.stripSortKeys(point)));
   //else new (&snapshotView) SnapshotView();
   window->render();
  };
  //view.status = [this](string status) { window->setTitle(status); };
  view.output = [this](string output) { this->output = output; };
  view.hover(view.parseDict("Pressure=80K,Radius=0.02,Seed=4,TimeStep=20µ,Angle=,Pattern=none,Wire="_));
 }
} app;
