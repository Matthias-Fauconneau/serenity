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
#include "sge.h"
#include <unistd.h>

generic size_t argmax(const ref<T>& a) { size_t argmax=0; for(size_t i: range(a.size)) if(a[i] > a[argmax]) argmax=i; return argmax; }

/*constexpr size_t medianWindowRadius = 2;
buffer<float> medianFilter(ref<float> source, size_t W=medianWindowRadius) {
 assert_(source.size > W+1+W);
 buffer<float> target(source.size-2*W);
 //target.slice(0, W).copy(source.slice(0, W));
 buffer<float> window(W+1+W);
 for(size_t i: range(W, source.size-W)) {
  window.copy(source.slice(i-W, W+1+W));
  target[i-W] = ::median(window); // Quickselect median mutates buffer
 }
 target.slice(target.size-W).copy(source.slice(source.size-W));
 return target;
}*/
buffer<float> medianFilter(ref<float> source) { return copyRef(source); }

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, float> axial;
 float min = inff, max = -inff;
 uint textSize;
 vec2 headerCellSize = vec2(80*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 array<string> dimensions[2];
 struct Target { Rect rect; Dict tableCoordinates; const Dict& key; };
 array<Target> targets;
 struct Header { Rect rect; Dict filter; };
 array<Header> headers;
 function<void(const Dict&, const Dict&)> hover, press;
 Folder cache {".cache", currentWorkingDirectory(), true};
 Dict point;
 Dict filter;

 ArrayView(string valueName, uint textSize=16) : valueName(valueName), textSize(textSize) {
  load();
 }

 // Prepends sort key
 void apply(Dict& dict) {
  if(dict.contains("Pattern"_)) { // Sort key
   int index = ref<string>{"none"_,"helix","spiral","radial"}.indexOf(dict.at("Pattern"));
   assert_(index >= 0 && index < 4, index, dict.at("Pattern"));
   dict.at("Pattern"_) = String( (char)index + (string)dict.at("Pattern"));
  }
 }

 // Applies sort key on load
 Dict parseDict(string id) {
  Dict configuration = ::parseDict(id);
  apply(configuration);
  return move(configuration);
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
  valueName = "Axial (Pa)"_;
  points.clear();
  Folder results ("."_);
  auto list = results.list(Files);
  for(string fileName: list) {
   if(!fileName.contains('.')) continue;
   if(endsWith(fileName,".stdout")) continue;
   string id = section(fileName,'.',0,-2);
   Dict configuration = parseDict(id);
   //if(configuration.at("Radius")!="30"_) continue;
   if(configuration.at("TimeStep")!="0.1"_) continue;
   if(configuration.at("grainShearModulus")!="800"_) continue;
   //if(points.contains(configuration)) continue;
   assert_(!points.contains(configuration), "Duplicate", configuration, points);
   array<char> data;
   if(0 && existsFile(id,cache) && File(id, cache).modifiedTime() >= realTime()-time*60e9)
    data = readFile(id, cache); // FIXME: do not reload old unchanged files
   if(!data) {
    String resultName;
    if(existsFile(id)) resultName = copyRef(id);
    if(!resultName) continue;
    /*if(resultName)*/ {
     map<string, array<float>> dataSets;
     TextData s (readFile(resultName));
     string resultLine = s.line();
     string headerLine = s.line();
     Dict results = parseDict(resultLine);
     /*if(results.contains("Wire density (%)"_))
      data.append((string)results.at("Wire density (%)"_));
     else
      data.append("0"_);*/
     buffer<string> names = split(headerLine,", "); // Second line: Headers
     for(string name: names) dataSets.insert(name);
     while(s) {
      for(size_t i = 0; s && !s.match('\n'); i++) {
       string d = s.whileDecimal();
       if(!d) goto break2;
       float decimal = parseDecimal(d);
       assert_(isNumber(decimal), s.slice(s.index-16,16),"|", s.slice(s.index));
       if(!(i < dataSets.values.size)) break;
       assert_(i < dataSets.values.size, i, dataSets.keys);
       dataSets.values[i].append( decimal );
       s.whileAny(' ');
      }
     }
break2:;
     ref<float> strain = dataSets.at("Strain (%)");
     float maxStrain = strain.last();
     data.append(str(maxStrain));
     //ref<float> stress = dataSets.at("Axial (Pa)");
     buffer<float> stress = medianFilter(dataSets.at("Axial (Pa)"));
     size_t argmax = ::argmax(stress); // !FIXME: max(stress) != max(stress-pressure)
     //log(stress[argmax]);
     data.append(" "_+str(stress[argmax]));
     float P = configuration.at("Pressure");
     data.append(" "_+str(P));
    } //else data.append("0 0 0"_);
    /*string logName;
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
    }*/
    assert_(data, fileName, id, resultName);
    log(id, data);
    writeFile(id, data, cache, true);
   }
   TextData s (data);
   /*float wireDensity = s.decimal();
   assert_(isNumber(wireDensity), s);
   s.skip(' ');*/
   float maxStrain = s.decimal();
   s.skip(' ');
   float peakStress = s.decimal();
   log(peakStress);
   assert_(isNumber(peakStress));
   s.skip(' ');
   /*float postPeakStress = s.decimal();
   assert_(isNumber(postPeakStress));
   s.skip(' ');*/
   float pressure = s.decimal();
   assert_(isNumber(pressure));
   /*float time=0, displacement=0;
   if(s) {
    s.skip(' ');
    time = s.decimal();
    assert_(isNumber(time));
    s.skip(' ');
    int unused id = s.integer();
    s.skip(' ');
    string unused state = s.word();
    s.skip(' ');
    displacement = s.decimal();
    if(displacement>=12.4) state = "done"__;
   }
   log(displacement);
   if(displacement<12) continue;*/
   if(maxStrain<12) continue;
   axial.insert(copy(configuration), peakStress);
   points.insert(move(configuration), peakStress);
  }

  if(points) {
   min = ::min(points.values);
   max = ::max(points.values);
  }
  dimensions[0] = copyRef(ref<string>{"TimeStep"_, "Radius"_, "Pressure"_, "Seed"_});
  dimensions[1] = copyRef(ref<string>{"linearSpeed","Pattern"_});
  for(auto& dimensions: this->dimensions) {
   dimensions.filter([this](const string dimension) {
    for(const Dict& coordinates: points.keys) if(coordinates.keys.contains(dimension)) return false;
    return true; // Filters unknown dimension
   });
  }
  assert(dimensions[0] && dimensions[1], dimensions, points);
 }

 /// Returns coordinates along \a dimension occuring in points matching \a filter
 array<Variant> coordinates(string dimension, const Dict& filter) const {
  array<Variant> allCoordinates;
  for(const Dict& coordinates: points.keys) if(coordinates.includes(filter)) {
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
  vec2 hint = vec2(levelCount().yx()+int2(1)) * headerCellSize
              + vec2(cellCount()) * contentCellSize;
  hint.x = -hint.x; // Expanding
  return hint;
 }

 uint renderHeader(Graphics& graphics, vec2 viewSize, vec2 contentCellSize, uint axis, uint level, Dict& filter, uint offset=0) {
  if(level==dimensions[axis].size) return 1;
  string dimension = dimensions[axis][level];
  assert_(!filter.contains(dimension));
  uint cellCount = 0;
  auto coordinates = this->coordinates(dimension, filter);
  for(const Variant& coordinate: coordinates) {
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
   headers.append(origin+Rect{size*cellSize}, copy(filter));
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
    bool running = false, pending = false;
    for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
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
     graphics.fills.append(cellOrigin, cellSize, color);

     Dict tableCoordinates = copy(filterX); tableCoordinates.append(copy(filterY));
     targets.append(Rect{cellOrigin, cellOrigin+cellSize}, move(tableCoordinates), coordinates);
     /*if(index==0)*/ value /= 1e3; // KPa
     String text = str(int(round(value))); //point.isInteger?dec(value):ftoa(value);
     if(pending) text = italic(text);
     if(running) text = bold(text);
     bgr3f textColor = 0;
     if(point && coordinates == point) textColor.b = 1;
     else if(filter && coordinates.includes(filter)) textColor.b = 1;
     graphics.graphics.insertMulti(cellOrigin, Text(text, textSize, textColor, 1, 0,
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

  if(1) {// Fixed coordinates in unused top-left corner
   array<char> fixed;
   for(const auto& coordinate: coordinates(points.keys)) {
    if(coordinate.value.size==1) fixed.append(coordinate.key+": "_+str(coordinate.value)+"\n"_);
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
  /*if(event == Press && (button == WheelUp || button == WheelDown)) {
   index=(index+4+(button==WheelUp?1:-1))%4;
   load();
   return true;
  }*/
  if(event == Press && button == LeftButton && press) {
      point = {};
      filter = {};
   for(const auto& target: targets) {
    if(target.rect.contains(cursor)) {
     point = copy(target.key);
     press(target.tableCoordinates, target.key);
     return true;
    }
   }
   for(const auto& target: headers) {
    if(target.rect.contains(cursor)) {
     filter = copy(target.filter);
     return true;
    }
   }
   press({},{});
  }
  if(event == Motion && hover) {
   for(auto& target: targets) {
    if(target.rect.contains(cursor)) {
     hover(target.tableCoordinates, target.key);
     return true;
    }
   }
   hover({},{});
  }
  return false;
 }
};

struct Review {
 ArrayView array {"Axial (MPa)"};
 Plot pressure;
 UniformGrid<Plot> strainPlots {1};
 VBox layout {{&array, &pressure, &strainPlots}};
 unique<Window> window = nullptr;

 Dict group, point;

 Plot pressurePlot(const Dict& point, /*size_t index,*/ size_t plotIndex/*=-1*/) {
  Dict filter = copy(point);
  if(filter.contains("Pressure"_)) filter.remove("Pressure");
  assert_(array.dimensions[0] && array.dimensions[1], array.dimensions);
  if(filter.contains(array.dimensions[0].last())) filter.remove(array.dimensions[0].last());
  if(filter.contains(array.dimensions[1].last())) filter.remove(array.dimensions[1].last());
  Plot plot;
  plot.xlabel = "Pressure (KPa)"__;
  plot.ylabel = /*Deviatoric*/"Maximum shear stress (KPa)"__;
  plot.plotPoints = true;
  plot.plotLines = true;// false;
  if(plotIndex!=invalid) {
   plot.plotCircles = true;
   plot.max = array.max;
  }
  ::array<Dict> points;
  for(const auto& point: array.points)
   if(point.key.includesPassMissing(filter)) points.append(copy(point.key));
  ::array<String> fixed;
  for(const auto& coordinate: array.coordinates(points))
   if(coordinate.value.size==1) fixed.append(copyRef(coordinate.key));
  for(const auto& point: points) {
   Dict shortSet = copy(point);
   for(string dimension: fixed) if(shortSet.contains(dimension)) shortSet.remove(dimension);
   if(shortSet.contains("Pressure")) shortSet.remove("Pressure");
   if(shortSet.contains(array.dimensions[0].last())) shortSet.remove(array.dimensions[0].last());
   if(array.dimensions[0].last()=="Radius"_)
    if(shortSet.contains("Height")) shortSet.remove("Height");
   String id = str(shortSet.values," "_,""_);
   auto& dataSet = plot.dataSets[::copy(id)];
   float peakStress = array.axial.at(point); // / 1000;
   float outsidePressure = float(point.at("Pressure")); // / 1000;
   float effectivePressure = outsidePressure;
   dataSet.insertSortedMulti((effectivePressure+peakStress)/2, (peakStress-effectivePressure)/2);
   log(point, effectivePressure, peakStress, (effectivePressure+peakStress)/2, (peakStress-effectivePressure)/2);
  }
  if(plotIndex!=invalid) {
   auto key = move(plot.dataSets.keys[plotIndex]);
   auto value = move(plot.dataSets.values[plotIndex]);
   plot.dataSets.clear();
   plot.dataSets.keys.append(move(key));
   plot.dataSets.values.append(move(value));
  }
  for(auto& key: plot.dataSets.keys)  if(key && key[0] < 16) key = copyRef(key.slice(1));
  //plot.uniformScale = true;
  plot.legendPosition = Plot::TopLeft;
  return plot;
 }

 Plot strainPlot(const Dict& point/*, size_t index*/) {
  map<string, ::array<float>> dataSets;
  String resultName;
  String id = str(array.stripSortKeys(point));
  if(existsFile(id+".failed")) resultName = id+".failed";
  if(existsFile(id+".working")) resultName = id+".working";
  if(existsFile(id+".result")) resultName = id+".result";
  if(existsFile(id)) resultName = copyRef(id);
  Plot plot;
  if(!resultName) { log("Missing result", id); return plot; }
  TextData s (readFile(resultName));
  if(!s) { log("Missing result", resultName); return plot; }
  // TODO: cache dataSets
  s.line();
  string headerLine = s.untilAny("\n0"); //s.line();
  buffer<string> names = split(headerLine,", ");
  for(string name: names) dataSets.insert(name);
  while(s) {
   for(size_t i = 0; s && !s.match('\n'); i++) {
    string d = s.whileDecimal();
    if(!d) goto break2;
    float decimal = parseDecimal(d);
    assert_(isNumber(decimal), s.slice(s.index-16,16),"|", s.slice(s.index));
    if(!(i < dataSets.values.size)) break;
    assert_(i < dataSets.values.size, i, dataSets.keys);
    dataSets.values[i].append( decimal );
    s.whileAny(' ');
   }
  }
  break2:;

  plot.xlabel = "Strain (%)"__;
  plot.max.x = 100./8;
  plot.min.y = 0, plot.max.y = 0.5;
  buffer<float> strain;
  if(dataSets.contains(plot.xlabel)) strain = move(dataSets.at("Strain (%)"));
  else {
   if(!dataSets.contains("Height (m)")) return plot;
   ref<float> height = dataSets.at("Height (m)");
   strain = apply(height, [=](float h){ return (1-h/height[0])*100; });
  }
 {
   //ref<float> stress = dataSets.at("Axial (Pa)");
   buffer<float> stress = medianFilter(dataSets.at("Axial (Pa)"));
   plot.ylabel = "Normalized Deviatoric Stress"__;
   buffer<float> deviatoric (strain.size);
   float pressure = float(point.at("Pressure"));
   for(size_t i: range(deviatoric.size)) deviatoric[i] = (stress[i]-pressure)/(stress[i]+pressure);
   plot.dataSets.insert(""__, {::move(strain), ::move(deviatoric)});
  }
  return plot;
 }

 Review() {
   auto group = array.parseDict("Radius=30,TimeStep=0.2");
   pressure = pressurePlot(group, -1);

  window = ::window(&layout, int2(0, 0), mainThread, true);
  window->actions[Key('x')] = [this](){
   array.dimensions[0] = ::array<string>(array.dimensions[0].slice(1) + array.dimensions[0][0]);
   window->render();
  };
  window->actions[Key('y')] = [this](){
   array.dimensions[1] = ::array<string>(array.dimensions[1].slice(1) + array.dimensions[1][0]);
   window->render();
  };
  window->actions[Key('f')] = [this](){
   window->widget = window->widget == &layout ? (Widget*)&pressure : &layout;
   window->render();
  };
  array.hover = [this](const Dict& group, const Dict& point) {
   /*if(hover)*/ array.press(group, point);
  };
  array.press = [this](const Dict& group, const Dict& point) {
   if(!point) return;

   this->group = copy(group);
   pressure = pressurePlot(group /*, pressurePlotIndex*/, -1);
   this->point = copy(point);
   strainPlots[0] = strainPlot(point);
   window->render();
   log(array.stripSortKeys(group));
   log(array.stripSortKeys(point));
  };
 }
} app;
