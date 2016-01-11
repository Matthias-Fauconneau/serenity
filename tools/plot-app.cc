#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "variant.h"
#include "time.h"

size_t medianWindowRadius = 0;
buffer<float> medianFilter(ref<float> source, size_t W=medianWindowRadius) {
 assert_(source.size > W+1+W);
 buffer<float> target(source.size-2*W);
 buffer<float> window(W+1+W);
 for(size_t i: range(W, source.size-W)) {
  window.copy(source.slice(i-W, W+1+W));
  target[i-W] = ::median(window); // Median quickselect mutates buffer
 }
 return target;
}

size_t meanWindowRadius = 8;
buffer<float> meanFilter(ref<float> source, size_t W=meanWindowRadius) {
 assert_(source.size > W+1+W);
 buffer<float> target(source.size-2*W);
 buffer<float> window(W+1+W);
 for(size_t i: range(W, source.size-W)) {
  float sum = 0;
  for(float v: source.slice(i-W, W+1+W)) sum += v;
  target[i-W] = sum/(W+1+W);
 }
 return target;
}

//FIXME
/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply2(ref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
 buffer<decltype(function(source[0]))> target(source.size); target.apply(function, source); return target;
}

struct PlotView : HList<Plot> {
 unique<Window> window = ::window(this, int2(0, 720));
 bool shown = true;
 size_t index = 0;
 FileWatcher watcher {"."_, [this](string){load();}};
 //Timer timer {[this]{load(); timer.setRelative(2000);}, 2}; // no inotify on NFS
 PlotView() {
  window->actions[Escape] = []{ exit_group(0); /*FIXME*/ };
  window->actions[F12] = {this, &PlotView::snapshot};
  window->actions[Space] = {this, &PlotView::load}; // no inotify on NFS
  window->actions[Key('m')] = [this]{
   medianWindowRadius = medianWindowRadius?64:0;
   load();
  };
  window->presentComplete = [this]{ shown=true; };
  load();
 }
 void snapshot() {
  String name = array<Plot>::at(0).ylabel+"-"+array<Plot>::at(0).xlabel;
  if(existsFile(name+".png"_)) log(name+".png exists");
  else {
   int2 size(1680, 1050);
   Image target = render(size, graphics(vec2(size), Rect(vec2(size))));
   writeFile(name+".png", encodePNG(target), home(), true);
  }
 }
 bool mouseEvent(vec2, vec2, Event, Button button, Widget*&) override {
  if(button == WheelUp || button == WheelDown) {
   index++;
   index = index%2;
   load();
   return true;
  }
  return false;
 }
 void load() {
  if(!shown) return;
  clear();
  static buffer<string> Y = split("Axial (Pa)" ", Radial (Pa)" ", Normalized deviator stress, Stress ratio"_,", ");
  for(size_t index: range(3, Y.size)) {
   Plot& plot = append();
   plot.xlabel = "Strain (%)"__;
   plot.ylabel = copyRef(Y[index]);
   map<String, array<Variant>> allCoordinates;
   for(Folder folder: {"."_}/*arguments()*/) {
    for(string name: folder.list(Files)) {
     if(name=="core") continue;
     if(endsWith(name,"stdout")) continue;
     auto parameters = parseDict(name);
     if(parameters.at("Radius")!="50"_) continue;
     if(parameters.at("sfStiffness")!="1M"_) continue;
     if(parameters.at("sfSpeed")!="100"_) continue;
     if(!(parameters.contains("nDamping") || parameters.contains("mDensity"))) continue;
      // FIXME: keeping only old versions
     for(const auto parameter: parameters)
      if(!allCoordinates[::copy(parameter.key)].contains(parameter.value))
       allCoordinates.at(parameter.key).insertSorted(::copy(parameter.value));
     for(string key: allCoordinates.keys) if(!parameters.contains(key)) allCoordinates.at(key).add(""_);
    }
    for(string name: folder.list(Files)) {
     if(name=="core") continue;
     if(endsWith(name,".stdout")) continue;
     if(endsWith(name,".png")) continue;
     auto parameters = parseDict(name);
     if(parameters.at("Radius")!="50"_) continue;
     if(parameters.at("sfStiffness")!="1M"_) continue;
     if(parameters.at("sfSpeed")!="100"_) continue;
     if(!(parameters.contains("nDamping") || parameters.contains("mDensity"))) continue;
      // FIXME: keeping only old versions
     TextData s (readFile(name, folder));
     s.until('\n'); // First line: constant results
     buffer<string> names = split(s.until('\n'),", "); // Second line: Headers
     map<string, array<float>> dataSets;
     for(string name: names) dataSets.insert(name);
     while(s) {
      for(size_t i = 0; s && !s.match('\n'); i++) {
       string d = s.whileDecimal();
       if(!d) goto break2;
       float decimal = parseDecimal(d);
       assert_(isNumber(decimal), decimal, d);
       assert_(i < dataSets.values.size, i, dataSets.keys);
       dataSets.values[i].append( decimal );
       s.whileAny(' ');
      }
     }
     break2:;
     //for(mref<float> data: dataSets.values) if(data) data[0]=0; // Filters invalid first point (inertia)
     for(auto data: dataSets) {
      if(data.key=="Axial (Pa)" || data.key == "Radial (Pa)")
       //data.value = medianFilter(data.value);
       data.value = meanFilter(data.value);
      else {
       //assert_(data.value.size >= 2*medianWindowRadius, data.value.size);
       //data.value = copyRef(data.value.slice(medianWindowRadius, data.value.size-2*medianWindowRadius));
       data.value = copyRef(data.value.slice(meanWindowRadius, data.value.size-2*meanWindowRadius));
      }
     }
     if(plot.ylabel == "Normalized deviator stress") {
      float pressure = parameters.at("Pressure");
      ref<float> radial = dataSets.at("Radial (Pa)");
      ref<float> axial = dataSets.at("Axial (Pa)");
      array<float>& ndeviator = dataSets.insert("Normalized deviator stress");
      ndeviator.grow(axial.size);
      if(0) for(int i: range(ndeviator.size)) ndeviator[i] = ((axial[i]-radial[i])/radial[i]);
      else for(int i: range(ndeviator.size)) ndeviator[i] = (axial[i]-pressure)/pressure;
     }
     if(plot.ylabel == "Stress ratio") {
      float pressure = parameters.at("Pressure");
      ref<float> radial = dataSets.at("Radial (Pa)");
      ref<float> axial = dataSets.at("Axial (Pa)");
      array<float>& ndeviator = dataSets.insert("Stress ratio");
      ndeviator.grow(axial.size);
      if(0) for(int i: range(ndeviator.size)) ndeviator[i] = (axial[i]-radial[i])/(axial[i]+radial[i]);
      else for(int i: range(ndeviator.size)) ndeviator[i] = (axial[i]-pressure)/(axial[i]+pressure);
     }
     assert_(dataSets.contains(plot.xlabel), plot.xlabel, name);
     assert_(dataSets.contains(plot.ylabel), plot.ylabel);
     for(string key: allCoordinates.keys) if(!parameters.contains(key)) allCoordinates.at(key).add(""_);
     parameters.filter([&](string key, const Variant&){ return allCoordinates.at(key).size==1; });
     //float key = dataSets.at(plot.ylabel).last();
     /*size_t i = plot.dataSets.size();//0; while(i<plot.dataSets.size() && plot.dataSets.values[i].values.last() <= key) i++;
     plot.dataSets.keys.insertAt(i, str(parameters,", "_));
     plot.dataSets.values.insertAt(i, map<float,float>{::move(dataSets.at(plot.xlabel)), ::move(dataSets.at(plot.ylabel))});*/
     plot.dataSets.insert(str(parameters,", "_), {::move(dataSets.at(plot.xlabel)), ::move(dataSets.at(plot.ylabel))});
     if(plot.ylabel=="Normalized deviator stress") { plot.min.y = 0; plot.max.y = 1; }
    }
   }
  }
  //if(count()) window->setTitle(array<Plot>::at(0).ylabel);
  shown = false;
  window->render();
  log("Loaded");
 }
} app;

