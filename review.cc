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

buffer<byte> toSVG(const Plot& plot) {
 array<byte> svg;
 svg.append("<?xml version='1.0'?><svg width='4cm' height='4cm' viewBox='0 0 1 1' xmlns='http://www.w3.org/2000/svg' version='1.2' baseProfile='tiny' "
                   "viewport-fill='white'>");
 for(const auto& dataSet: plot.dataSets.values) {
  svg.append("<path fill='none' stroke-width='0.01' stroke='black' d='M"); //vector-effect='non-scaling-stroke'
  array<float> X, Y;
  for(const auto point: dataSet) {
   X.append(point.key);
   Y.append(point.value);
  }
  log(max(X), max(Y));
  {
   float x = X[0] / max(X);
   float y = Y[0] / max(Y);
   svg.append(str(x, y)+" L");
  }
  for(size_t i: range(dataSet.size())) {
   float x = X[i] / max(X);
   float y = Y[i] / max(Y);
   svg.append(str(x, y)+" ");
  }
  svg.append("'/>");
 }
 svg.append("</svg>");
 return move(svg);
}

buffer<byte> toPDF(Widget& widget, vec2 pageSizeMM=210/*mm*/) {
 static constexpr float pointMM = 72 / 25.4;
 vec2 pageSize = pageSizeMM*pointMM;
 shared<Graphics> graphics = widget.graphics(pageSize, Rect(pageSize));
 graphics->flatten();
 return toPDF(pageSize, ref<Graphics>(graphics.pointer, 1), 1);
}

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
 double mx = mean(X), my = mean(Y);
 size_t N = X.size;
 if(N<=1) return {0,0};
 assert_(N>1);
 double sxx=0; for(float x: X) sxx += sq(x-mx); sxx /= (N-1);
 double sxy=0; for(size_t i: range(N)) sxy += (X[i]-mx)*(Y[i]-my); sxy /= (N-1);
 double syy=0; for(float y: Y) syy += sq(y-my); syy /= (N-1);
 double a = (syy - sxx + sqrt(sq(syy-sxx)+4*sq(sxy))) / (2*sxy);
 double b = my - a*mx;
 return {a, b};
}

buffer<float> radius(const map<string, array<float>>& data) {
 ref<float> radius = data.at("Radius (m)"_);
 // FIXME: only < 14.8 15:
 return apply(radius, [](float r){return r/*+float(2.47e-3)*//*Grain::radius*/;});
}

buffer<float> stress(const map<string, array<float>>& data, bool useMedianFilter=true) {
 buffer<float> stress;
 if(data.contains("Stress (Pa)")) stress = copyRef(data.at("Stress (Pa)"));
 else {
  ref<float> force = data.at("Plate Force (N)"_);
  buffer<float> radius = ::radius(data);
  if(radius) {
      float A0 = 2* PI*sq(radius[0]); //(float)point.at("Radius"));
      //ref<float> height = data.at("Height (m)"_);
      //float h0 = height[0];
      //float V0 = height[0] * float(PI) * sq(radius[0]);
      stress = ::apply(force.size, [&](size_t i){
              //float V = height[i] * float(PI) * sq(radius[i]);
              //float Ac = A0 * (V/V0) / (height[i]/h0);
              return force[i] / A0; //Ac;
      });
  }
 }
 if(useMedianFilter && stress.size > 2*medianWindowRadius+1) {
  stress = medianFilter(stress);
 }
 return  stress;
}

struct ArrayView : Widget {
 string valueName;
 map<Dict, float> points; // Data points
 map<Dict, String> ids; // Job IDs
 map<Dict, String> states; // Job states
 map<Dict, float> displacements;
 map<Dict, float> pressure;
 array<SGEJob> jobs;
 float minDisplacement = inf, commonDisplacement = 0;
 float min = inf, max = -inf;
 uint textSize;
 vec2 headerCellSize = vec2(80*textSize/16, textSize);
 vec2 contentCellSize = vec2(48*textSize/16, textSize);
 array<string> dimensions[2];
 struct Target { Rect rect; Dict tableCoordinates; const Dict& key; };
 array<Target> targets;
 struct Header { Rect rect; Dict filter; };
 array<Header> headers;
 size_t index = 0;
 function<void(const Dict&, const Dict&)> hover, press;
 Folder cache {".cache", currentWorkingDirectory(), true};
 Dict point;
 Dict filter;

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
  valueName = ref<string>{"Stress (Pa)", "Time (s)", "Wire density (%)", "Displacement (%)"}[index];
  points.clear();
  ids.clear();
  states.clear();
  pressure.clear();
  displacements.clear();
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
   //if(existsFile(id+".result") && File(id+".result").modifiedTime() < realTime()-12*60*60e9) continue;
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
      buffer<float> stress = ::stress(dataSets, true);
      if(stress) {
       size_t argmax = ::argmax(stress); // !FIXME: max(stress) != max(stress-pressure)
       data.append(" "_+str(stress[argmax]));
       data.append(" "_+str(mean(stress.slice(argmax))));

       if(!dataSets.contains("Radial Force (N)"_)) { log(dataSets.keys); continue; }
       ref<float> force = dataSets.at("Radial Force (N)"_);
       buffer<float> radius = ::radius(dataSets);
       ref<float> height = dataSets.at("Height (m)"_);
       buffer<float> pressure = ::apply(force.size, [&](size_t i) {
        return force[i] / (height[i] * float(2*PI) * radius[i]); });
       float P = mean(pressure.slice(argmax));
       assert_( P >= 0 );
       //float P = pressure[argmax];
       //float P = ::max(pressure); // TODO: median filter
       //assert_(P > 0);
       data.append(" "_+str(P));
      } else data.append(" 0 0 0"_);
     } else data.append("0 0 0 0"_); // if(s)
    } else data.append("0 0 0 0"_); // if(resultName)
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
    assert_(data, index, fileName, id, resultName, logName);
    log(id, data);
    writeFile(id, data, cache, true);
   }
   TextData s (data);
   float wireDensity = s.decimal();
   assert_(isNumber(wireDensity), s);
   s.skip(' ');
   float peakStress = s.decimal();
   assert_(isNumber(peakStress));
   s.skip(' ');
   float postPeakStress = s.decimal();
   assert_(isNumber(postPeakStress));
   s.skip(' ');
   float pressure = s.decimal();
   assert_(isNumber(pressure));
   this->pressure.insert(copy(configuration), pressure);
   float time=0, displacement=0;
   if(s) {
    s.skip(' ');
    time = s.decimal();
    assert_(isNumber(time));
    s.skip(' ');
    ids.insert(copy(configuration), str(s.integer()));
    s.skip(' ');
    states.insert(copy(configuration), copyRef(s.word()));
    s.skip(' ');
    displacement = s.decimal();
    displacements.insert(copy(configuration), displacement);
    minDisplacement = ::min(minDisplacement, displacement);
    if(displacement>=12.4) states.at(configuration) = "done"__;
   }
   points.insert(move(configuration), ref<float>{postPeakStress,time,wireDensity,displacement}[index]);
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
  dimensions[0] = copyRef(ref<string>{"Friction"_, "TimeStep"_, "Radius"_, /*"Elasticity",*/ "Pressure"_, "Seed"_});
  dimensions[1] = copyRef(ref<string>{"Resolution"_, "Side","Thickness","Wire"_,"Rate"_,"Angle"_,"Pattern"_});
  for(auto& dimensions: this->dimensions) {
   dimensions.filter([this](const string dimension) {
    for(const Dict& coordinates: points.keys) if(coordinates.keys.contains(dimension)) return false;
    return true; // Filters unknown dimension
   });
  }

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

 void remove(const Dict& key) {
     assert_(key);
     String id = str(stripSortKeys(key));
     assert_(id);
     for(string name: Folder(".").list(Files)) if(startsWith(name, id)) {
         log(name);
         //fileCount++;
         rename(currentWorkingDirectory(), name, \
                Folder("../Archive", currentWorkingDirectory(), true), name);
     }

 }

 void removeFailed() {
  size_t failureCount = 0, fileCount = 0;
  for(const Dict& key: points.keys) {
   if(states.contains(key) && states.at(key)!="done"_ && !jobs.contains(key)) {
       remove(key);
    failureCount++;
   }
  }
  log(failureCount, fileCount);
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
      else if(state=="load") {
       color = bgr3f(7./8);
       if(displacements.contains(coordinates)) {
        float alpha = displacements.at(coordinates)/(100./8);
        assert_(alpha >= 0 && alpha <= 1, alpha);
        color = bgr3f(7./8 + alpha/8);
       }
      }
      else if(state=="done") color = bgr3f(1);
      else if(state) log("Unknown state", state);
      if(!running && state!="done") {color.r=1; color.b=color.g=1./2;}
     }
     graphics.fills.append(cellOrigin, cellSize, color);

     Dict tableCoordinates = copy(filterX); tableCoordinates.append(copy(filterY));
     targets.append(Rect{cellOrigin, cellOrigin+cellSize}, move(tableCoordinates), coordinates);
     if(index==0) value /= 1e6; // MPa
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
 ArrayView array {"Stress (MPa)"};
 Plot pressure;
 SnapshotView snapshotView;
 Text output;
 UniformGrid<Plot> strainPlots {4};
 //VList<Plot> strainPlots {4};
 //HBox hbox1 {{&pressure, &strainPlots}};
 VBox vbox {{&pressure,/*&hbox1,*/&strainPlots, &output}};
 HBox hbox {{&vbox, &snapshotView}};
 VBox layout {{&array, &hbox/*, &strainPlots*//*, &output*/}/*, VBox::Even*/};
 unique<Window> window = nullptr;// = ::window(&layout, int2(0, 0), mainThread, true);

 bool useMedianFilter = true;
 enum { Pressure, Stress, Deviatoric, Volume, Length };
 size_t pressurePlotIndex = Deviatoric;
 size_t strainPlotIndex = Length;
 Dict group, point;
 bool details = true, hover = true;

 Plot pressurePlot(const Dict& point, size_t index, size_t plotIndex=-1) {
  Dict filter = copy(point);
  if(filter.contains("Pressure"_)) filter.remove("Pressure");
  if(filter.contains(array.dimensions[0].last())) filter.remove(array.dimensions[0].last());
  if(filter.contains(array.dimensions[1].last())) filter.remove(array.dimensions[1].last());
  Plot plot;
  plot.xlabel = "Pressure (KPa)"__;
  if(index==Stress) {
   plot.ylabel = "Stress (Pa)"__;
   plot.plotBandsY = true;
   plot.max.y = array.max;
  } else if(index==Deviatoric) {
   plot.ylabel = /*Deviatoric*/"Stress (KPa)"__;
   plot.plotPoints = true;
   plot.plotLines = false;
   if(plotIndex!=invalid) {
    plot.plotCircles = true;
    plot.max = array.max;
   }
  } else if(index==Pressure) {
   plot.ylabel = "Pressure (Pa)"__;
   plot.plotBandsY = false;
   plot.plotBandsX = false;
   plot.plotCircles = false;
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
   float peakStress = array.points.at(point) / 1000;
   if(peakStress <= 0) continue;
   bool running = false;
   //for(const auto& job: array.jobs) if(job.dict == point) if(job.state=="running" || job.state=="pending") { log("running"); running=true; }
   if(running) continue;
   float outsidePressure = float(point.at("Pressure")) / 1000;
   float effectivePressure = array.pressure.at(point) / 1000;
   assert_(effectivePressure >= 0, effectivePressure);
   /***/if(index==Pressure)
    dataSet.insertSortedMulti(outsidePressure, effectivePressure);
   else if(index==Deviatoric) {
    if( (peakStress-effectivePressure)/2 < 0) continue;
    dataSet.insertSortedMulti((effectivePressure+peakStress)/2, (peakStress-effectivePressure)/2);
   } else if(index==Stress)
    dataSet.insertSortedMulti(effectivePressure, peakStress);
   log(point);
  }
  if(plotIndex!=invalid) {
   auto key = move(plot.dataSets.keys[plotIndex]);
   auto value = move(plot.dataSets.values[plotIndex]);
   plot.dataSets.clear();
   plot.dataSets.keys.append(move(key));
   plot.dataSets.values.append(move(value));
  }
  if(index==Deviatoric && plot.dataSets) {
   //const size_t N = 16;
   //buffer<map<NaturalString, map<float, float>>> tangents (N); tangents.clear();

   for(auto entry: plot.dataSets) {
    ref<float> X = entry.value.keys, Y = entry.value.values;
#if 0
    buffer<float> x (X.size), y (Y.size);
    float bestSSR = inf; //φ = 0,
    Fit bestFit;
    for(size_t angleIndex: range(N)) {
     float φ = PI/2*angleIndex/N;
     for(size_t i: range(X.size)) {
      x[i] = X[i] - Y[i]*sin(φ); y[i] = Y[i]*cos(φ);
      tangents[angleIndex][copy(entry.key)+" fit"+str(angleIndex)].insertSortedMulti(x[i], y[i]);
     }
     auto f = totalLeastSquare(x, y);
     float a=f.a, b=f.b;
     float SSR = 0;
     for(size_t i: range(X.size)) {
      // Distance to circle tangent point
      float xi = (X[i] - a*b) / (a*a + 1);
      if(xi < 0) { SSR=inf; break; }
      float yi = a*xi + b;
      float R = Y[i];
      float r = sqrt(sq(xi-X[i])+sq(yi));
      float d = R - r;
      SSR +=  d;
     }
     if(SSR < bestSSR) {
      bestSSR = SSR;
      bestFit = f;
     }
     //plot.fits[copy(entry.key)].append(f);
    }
   }
   //plot.dataSets.append(move(tangents[bestIndex]));
   for(auto& t: tangents) plot.dataSets.append(move(t));
#elif 0
    if(X) {
     const size_t N = 256;
     buffer<float> sX (N), sY (N);
     float max = X.last();
     for(size_t sampleIndex: range(N)) {
      float x = max * sampleIndex/(N-1);
      float y = 0;
      for(size_t circleIndex: range(X.size)) {
       float y2 = sq(Y[circleIndex]) - sq(x - X[circleIndex]);
       if(y2 > 0) y = ::max(y, sqrt(y2));
      }
      sX[sampleIndex] = x;
      sY[sampleIndex] = y;
     }
     plot.fits[copy(entry.key)].append(totalLeastSquare(sX, sY));
    }
#else
    if(X) {
     plot.fits[copy(entry.key)].append(totalLeastSquare(X, Y));
    }
#endif
   }
  }
  for(auto& key: plot.dataSets.keys)  if(key && key[0] < 16) key = copyRef(key.slice(1));
  plot.uniformScale = true;
  return plot;
 }

 Plot strainPlot(const Dict& point, size_t index) {
  map<string, ::array<float>> dataSets;
  String resultName;
  String id = str(array.stripSortKeys(point));
  if(existsFile(id+".failed")) resultName = id+".failed";
  if(existsFile(id+".working")) resultName = id+".working";
  if(existsFile(id+".result")) resultName = id+".result";
  Plot plot;
  if(!resultName) { log("Missing result", id); return plot; }
  TextData s (readFile(resultName));
  if(!s) { log("Missing result", resultName); return plot; }
  // TODO: cache dataSets
  s.line();
  string headerLine = s.line();
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
  buffer<float> strain;
  if(dataSets.contains(plot.xlabel)) strain = move(dataSets.at("Strain (%)"));
  else {
   ref<float> height = dataSets.at("Height (m)");
   strain = apply(height, [=](float h){ return (1-h/height[0])*100; });
  }

  if(index == Volume) {
   plot.ylabel ="Volumetric Strain (%)"__;
   buffer<float> volume;
   if(dataSets.contains("Volumetric Strain (%)"_)) {
    volume = move(dataSets.at(plot.ylabel));
    for(float& v: volume) v=-v;
   } else {
    buffer<float> radius = ::radius(dataSets);
    ref<float> height = dataSets.at("Height (m)"_);
    float v0 = height[0] * float(PI) * sq(radius[0]);
    volume = apply(strain.size, [&](size_t i){
     return (1 - (height[i] * float(PI) * sq(radius[i]))/v0)*100; });
   }
   plot.dataSets.insert(""__, {::move(strain), ::move(volume)});
  }
  else if(index==Length) {
   plot.ylabel = "Radius (%)"__;
   buffer<float> radius = ::radius(dataSets);
   buffer<float> radiusStrain = apply(radius.size, [&](size_t i){
    return (radius[i]/radius[0] - 1)*100; });
   plot.dataSets.insert("Radius"__, {::move(strain), ::copyRef(radiusStrain)});
  }
  else if(index==Pressure) {
   plot.ylabel = "Pressure (Pa)"__;
   ref<float> force = dataSets.at("Radial Force (N)"_);
   strain.size = min(strain.size, force.size);
   buffer<float> radius = ::radius(dataSets);
   ref<float> height = dataSets.at("Height (m)"_);
   buffer<float> pressure (strain.size);
   for(size_t i: range(pressure.size)) pressure[i] = force[i] / (height[i] * 2 * PI * radius[i]);
   if(useMedianFilter && pressure.size > 2*medianWindowRadius+1) {
    const size_t medianWindowRadius = 4;
    pressure = medianFilter(pressure , medianWindowRadius);
    strain = copyRef(strain.slice(medianWindowRadius, pressure.size-2*medianWindowRadius));
   }
   plot.dataSets.insert(""__, {::move(strain), ::move(pressure)});
  }
  else {
   buffer<float> stress = ::stress(dataSets);
   if(useMedianFilter  && strain.size > 2*medianWindowRadius+1)
      strain = copyRef(strain.slice(medianWindowRadius, strain.size-2*medianWindowRadius));
   if(index==Stress) {
    plot.ylabel = "Stress (Pa)"__;
    plot.min.y = 0; plot.max.y = array.max;
    plot.dataSets.insert(""__, {::move(strain), ::move(stress)});
   }
   else if(index==Deviatoric) {
    ref<float> force = dataSets.at("Radial Force (N)"_);
    buffer<float> radius = ::radius(dataSets);
    ref<float> height = dataSets.at("Height (m)"_);
    buffer<float> pressure (force.size);
    for(size_t i: range(pressure.size)) {
        pressure[i] = force[i] / (height[i] * 2 * PI * radius[i]);
        assert_(pressure[i] >= 0);
    }
    if(useMedianFilter && pressure.size > 2*medianWindowRadius+1) {
     const size_t medianWindowRadius = 4;
     pressure = medianFilter(pressure , medianWindowRadius);
    }
    plot.ylabel = "Normalized Deviatoric Stress"__;
    buffer<float> deviatoric (strain.size);
    assert_(strain.size <= pressure.size, strain.size, pressure.size);
    for(size_t i: range(deviatoric.size)) deviatoric[i] = (stress[i]-pressure[i])/(stress[i]+pressure[i]);
    plot.min.y = 0; plot.max.y = 1;
    plot.dataSets.insert(""__, {::move(strain), ::move(deviatoric)});
   }
   else error(index);
  }
  return plot;
 }

 Review() {
  if(0) {
   auto point = array.parseDict("Friction=0.3,Pattern=none,Pressure=80K,Radius=0.02,Rate=100,Resolution=2,Seed=1,Side=1e8,Thickness=1e-3,TimeStep=10µ");
   writeFile("none80.svg"_, toSVG(strainPlot(point, Deviatoric)), home(), true);
   //writeFile("none80.pdf"_, toPDF(strainPlot(point, Deviatoric)), home(), true);
   error("plot");
  }

  if(1) {
   auto group = array.parseDict("Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=cross,Pressure=60K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Thickness=1e-3,TimeStep=10µ,Wire=12%");
   if(1) {
    if(1) {
     for(size_t index: range(4)) {
      Plot plot = pressurePlot(group, Deviatoric, index);
      plot.max = vec2(200, 100);
      String name = copyRef(plot.dataSets.keys[0]);
      plot.dataSets.keys[0] = ""__; /*copyRef(ref<string>{"No Wire"_, "Simple Helix"_,"Spiral Helix"_,"Radially Reinforced Helix"_}[
                                     ref<string>{"none"_,"helix","loop","cross"}.indexOf(name)]);*/
      writeFile(name+".pdf"_, toPDF(plot, vec2(94.5, 94.5/1.5)), home(), true);
      //error(name);
     }
    } else {
     VList<Plot> plots (Linear::Share, Linear::Expand);
     for(size_t index: range(1)) {
      auto& plot = plots.append(pressurePlot(group, Deviatoric, index));
      plot.max = vec2(200, 100);
      String& name = plot.dataSets.keys[0];
      name = ""__; /*copyRef(ref<string>{"Without wire"_, "Simple helix"_,"Spiral helix"_,"Radially reinforced helix"_}[
                                                      ref<string>{"none"_,"helix","loop","cross"}.indexOf(name)]);*/
     }
     writeFile("none.pdf"_, toPDF(plots, vec2(94.5, 94.5/*267.3*/)), home(), true);
    }
   }
   Plot plot = pressurePlot(group, Deviatoric);
   log(plot.dataSets.keys);
   writeFile("plot.pdf"_, toPDF(plot, vec2(94.5, 94.5/1.5)), home(), true);
   error("plot");
  }

  window = ::window(&layout, int2(0, 0), mainThread, true);
  window->actions[Key('d')] = [this](){
   details=!details;
   if(details) {
    for(size_t i : range(4)) strainPlots[i] = strainPlot(point, (strainPlotIndex+i)%5);
    snapshotView = SnapshotView(str(array.stripSortKeys(point)));
   } else {
    for(size_t i : range(4)) strainPlots[i] =Plot();
    snapshotView = SnapshotView();
   }
   window->render();
  };
  window->actions[Key('m')] = [this](){
      useMedianFilter=!useMedianFilter;
      array.press(group, point);
      window->render();
  };
  window->actions[Key('p')] = [this](){
   pressurePlotIndex = (pressurePlotIndex+1)%3;
   pressure = pressurePlot(group, pressurePlotIndex);
   window->render();
  };
  window->actions[Key('s')] = [this](){
   strainPlotIndex = (strainPlotIndex+1)%5;
   for(size_t i : range(4)) strainPlots[i] = strainPlot(point, (strainPlotIndex+i)%5);
   window->render();
  };
  window->actions[Key('x')] = [this](){
   array.dimensions[0] = ::array<string>(array.dimensions[0].slice(1) + array.dimensions[0][0]);
   window->render();
  };
  window->actions[Key('y')] = [this](){
   array.dimensions[1] = ::array<string>(array.dimensions[1].slice(1) + array.dimensions[1][0]);
   window->render();
  };
  window->actions[Key('h')] = [this](){ hover=!hover; };
  window->actions[Key('f')] = [this](){
   window->widget = window->widget == &layout ? (Widget*)&pressure : &layout;
   window->render();
  };
  window->actions[Key('v')] = [this](){
   window->widget = window->widget == &layout ? (Widget*)&snapshotView : &layout;
   window->render();
  };
  window->actions[Delete] = [this]() {
      if(array.point) {
          array.remove(array.point);
          if(array.jobs.contains(array.point)) {
              new SSH({"qdel","-f", array.jobs[array.jobs.indexOf(array.point)].id}, true); // FIXME: leak
              array.jobs = qstat(0);
              array.jobs.remove(array.point);
          }
          array.load(0);
      } else if(array.filter) {
          ::array<char> ids;
          for(const Dict& key: array.points.keys) {
              if(key.includes(array.filter)) {
                  array.remove(key);
                  log(array.stripSortKeys(key));
                  if(array.jobs.contains(key))
                      ids.append(array.jobs[array.jobs.indexOf(key)].id+" ");
              }
          }
          log("qdel -f",ids,"&");
          new SSH({"qdel","-f", ids}, true); // FIXME: leak
          array.load(0);
      } else {
          window->setTitle("Refreshing");
          array.jobs = array.qstat(0); array.load(0);
          window->setTitle("Deleting");
          array.removeFailed(); array.load(0);
          window->setTitle(str(array.jobs.size));
          array.load();
      }
   window->render();
  };
  window->actions[Return] = [this](){
   window->setTitle("Refreshing");
   array.jobs = array.qstat(0);
   array.load(0);
   window->setTitle(str(array.jobs.size));
   window->render();
  };
  window->actions[Space] = [this](){
   //remove("plot.pdf"_, home());
   static constexpr float inchMM = 25.4, inchPx = 90;
   const vec2 pageSize (210/*mm*/ * (inchPx/inchMM), 210/*mm*/ * (inchPx/inchMM));
   //auto graphics = layout.graphics(pageSize, Rect(pageSize));
   auto graphics = pressure.graphics(pageSize);
   graphics->flatten();
   writeFile("plot.pdf"_, toPDF(pageSize, ref<Graphics>(graphics.pointer, 1), 72 / inchPx /*px/inch*/), home(), true);
  };
  array.hover = [this](const Dict& group, const Dict& point) {
   if(hover) array.press(group, point);
  };
  array.press = [this](const Dict& group, const Dict& point) {
   if(!point) return;

   this->group = copy(group);
   pressure = pressurePlot(group, pressurePlotIndex);
   this->point = copy(point);
   if(details) {
    for(size_t i : range(4)) strainPlots[i] = strainPlot(point, (strainPlotIndex+i)%5);
   }

   if(array.ids.contains(point)) {
    String file = str(array.stripSortKeys(point))+".o"+str(array.ids.at(point));
    if(existsFile(file))
     output = Text(/*bold*/(str(array.stripSortKeys(point)))+'\n'+trim(section(readFile(file),'\n',-10,-1)));
    else log("Missing output", file);
   } else log("Missing ID", point);

   if(details) {
       buffer<String> files = Folder(".").list(Files|Sorted);
    String lastSnapshot; int64 lastTime = 0;
    for(string file: files) if(startsWith(file, str(array.stripSortKeys(point)))) {
     if(endsWith(file, ".grain") || endsWith(file, ".side") /*|| endsWith(file, ".wire")*/) {
         int64 time = File(file).modifiedTime();
         if(time >= lastTime) {
             lastTime = time;
             lastSnapshot = copyRef(file);
         }
         else
             assert_(lastSnapshot);
     }
     log(file);
    }
    snapshotView = SnapshotView(section(lastSnapshot,'.',0,-2));
   } else snapshotView = SnapshotView();
   window->render();
   log(array.stripSortKeys(group));
   log(array.stripSortKeys(point));
  };
 }
} app;
