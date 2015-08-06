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
    assert_(!existsFile(newName));
    rename(name, newName, results);
   }
  }
 }
} app;
#endif

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, String> ids; // Job IDs
 map<Dict, String> states; // Job states
 array<SGEJob> jobs;
 float min = inf, max = -inf;
 uint textSize;
 vec2 headerCellSize = vec2(80*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 array<string> dimensions[2];
 struct Target { Rect rect; Dict tableCoordinates; const Dict& key; };
 array<Target> targets;
 size_t index = 0;
 function<void(const Dict&)> hover, press;
 function<void(string)> status;
 Folder cache {".cache", currentWorkingDirectory(), true};

 ArrayView(string valueName, uint textSize=16)
  : valueName(valueName), textSize(textSize) {
  jobs = qstat(30);
  load();
 }
 void load(int time=20) {
  valueName = ref<string>{"Stress (MPa)", "Time (s)", "Wire density (%)", "Displacement (%)"}[index];
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
   assert_(id, fileName);
   Dict configuration = parseDict(id);
   if(!configuration.contains("Seed")) continue;
   if(!configuration.contains("Pattern")) continue;
   //if(!existsFile(str(configuration))) continue; // Only with snapshot signal file
   if(points.contains(configuration)) {
    //log("Duplicate configuration", configuration);
    continue;
   }
   array<char> data;
   if(1 && existsFile(id,cache) && File(id, cache).modifiedTime() >= realTime()-time*60e9)
    data = readFile(id, cache);
   if(!data) {
    String resultName;
    if(existsFile(id+".failed")) resultName = id+".failed";
    if(existsFile(id+".working")) resultName = id+".working";
    if(existsFile(id+".result")) resultName = id+".result";
    if(resultName) {
     map<string, array<float>> dataSets;
     TextData s (readFile(resultName));
     if(s) {
      log(id);
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
      if(dataSets.at("Stress (Pa)"_))
       data.append(" "_+str(::max(dataSets.at("Stress (Pa)"_))));
      else
       data.append(" 0"_);
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
    if(displacement>=12.4) states.at(configuration) = "done"__;
   }
   /*if(points.contains(configuration)) {
    if(1 || points.at(configuration)!=0) { log("Duplicate configuration", configuration); continue; }
    else points.at(configuration) = value; // Replace 0 from log
   } else*/
   points.insert(move(configuration), ref<float>{stress,time,wireDensity,displacement}[index]);
  }

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
  dimensions[0] = copyRef(ref<string>{"Radius"_,"Pressure"_});
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

  //array<String> failures;
  size_t failureCount = 0, fileCount = 0;
  for(const Dict& key: points.keys) {
   if(states.contains(key) && states.at(key)!="done"_ && !jobs.contains(key)) {
    //logInfo(key);
    //failures.append(str(key));
    for(string name: Folder(".").list(Files)) if(startsWith(name, str(key))) {
     log(name);
     fileCount++;
     //remove(name);
    }
    //assert_(existsFile(str(key)+".working"));
    //remove(str(key)+".working");
    failureCount++;
   }
  }
  //log(failures.size);
  log("Failed configuration", failureCount, "files", fileCount);
 }

 void removeFailed() {
  //array<String> failures;
  size_t failureCount = 0, fileCount = 0;
  for(const Dict& key: points.keys) {
   if(states.at(key)!="done"_ && !jobs.contains(key)) {
    //logInfo(key);
    //failures.append(str(key));
    for(string name: Folder(".").list(Files)) if(startsWith(name, str(key))) {
     log(name);
     fileCount++;
     remove(name);
    }
    //assert_(existsFile(str(key)+".working"));
    //remove(str(key)+".working");
    failureCount++;
   }
  }
  //log(failures.size);
  log(failureCount, fileCount);
 }

 void logInfo(const Dict& key) {
  log(key);
  if(states.contains(key)) log(states.at(key));
  if(ids.contains(key)) {
   if(jobs.contains(key)) log(jobs[jobs.indexOf(key)]);
   String name = str(key)+".o"+str(ids.at(key));
   if(existsFile(name)) log(section(readFile(name),'\n',-10,-1));
  }
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
   graphics.graphics.insertMulti(origin, Text(str(coordinate), textSize, 0, 1, 0,
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
     if(existsFile(str(coordinates))) hasSnapshotSignalFile = true;
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
     //if(existsFile(str(coordinates))) text = bold(text);
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
 HBox detail {{&plot, &snapshotView}};
 VBox layout {{&view, &detail}};
 unique<Window> window = ::window(&layout, int2(0, 1050), mainThread, true);
 Review() {
  window->actions[Key('x')] = [this](){
   view.dimensions[0] = array<string>(view.dimensions[0].slice(1) + view.dimensions[0][0]);
   window->render();
  };
  window->actions[Key('y')] = [this](){
   view.dimensions[1] = array<string>(view.dimensions[1].slice(1) + view.dimensions[1][0]);
   window->render();
  };
  window->actions[Delete] = [this]() { view.removeFailed(); view.load(10); window->render(); };
  window->actions[Return] = [this](){ view.jobs = qstat(0); view.load(10); window->render(); };
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
   filter.remove("Pressure");
   //filter.filter([](string, const Variant& value){ return !value; });
   if(filter.contains(view.dimensions[1].last())) filter.remove(view.dimensions[1].last());
   plot.xlabel = "Pressure (N)"__;
   plot.ylabel = "Stress (Pa)"__;
   plot.dataSets.clear();
   plot.min.y = 0, plot.max.y = view.max;
   array<Dict> points;
   //auto patterns = view.coordinates("Pattern", filter);
   Dict parameters = copy(filter);
   //for(const Variant& pattern: patterns) {
    //parameters[copyRef("Pattern"_)] = copy(pattern);
    auto pressures = view.coordinates("Pressure", filter);
    for(const Variant& pressure: pressures) {
     Dict key = copy(parameters);
     key.insertSorted(copyRef("Pressure"_), copy(pressure));
     for(const auto& point: view.points) {
      if(point.key.includesPassMissing(key)) {
       points.append(copy(point.key));
      }
     }
    }
   //}
   array<String> fixed;
   for(const auto& coordinate: view.coordinates(points))
    if(coordinate.value.size==1) fixed.append(copyRef(coordinate.key));
   for(const auto& point: points) {
    Dict shortSet = copy(point);
    for(string dimension: fixed) if(shortSet.contains(dimension)) shortSet.remove(dimension);
    if(shortSet.contains("Pressure")) shortSet.remove("Pressure");
    if(!shortSet.contains("Seed")) shortSet.insert("Seed"__,"1"__);
    shortSet.remove("Seed");
    auto& dataSet = plot.dataSets[str(shortSet.values," "_,""_)];
    float maxStress = view.points.at(point);
    if(maxStress) dataSet.insertSortedMulti(float(point.at("Pressure")), maxStress);
   }

   window->render();
   //window->actions[Space]();
  };
  view.press = [this](const Dict& point) {
   snapshotView.~SnapshotView();
   if(point && existsFile(str(point))) {
    //assert_(existsFile(localPath), localPath);
    /*File(localPath).write("S"_); // FIFO over NFS ?
   String remotePath = "/home/"_+user()+"/Results/"+str(point);
   int status = execute("/usr/bin/ssh",ref<string>{arguments()[0],"sh"_,"-c"_,"echo S > "+remotePath});
   assert_(status == 0);*/
    int64 time = realTime();
    File file (str(point));
    file.touch(time);
    /*do { // Waits for job to dump snapshot. FIXME: NFS cache does not update, FIXME: signal back
     log(".");
     usleep(100*1000);
    } while(file.modifiedTime()==time);*/
    usleep(300*1000); // FIXME: signal back
    new (&snapshotView) SnapshotView(str(point));
   } else new (&snapshotView) SnapshotView();
   window->render();
  };
  view.status = [this](string status) { window->setTitle(status); };
 }
}
#if !RENAME
app
#endif
;
