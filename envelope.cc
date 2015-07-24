#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "variant.h"

//FIXME
/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply2(ref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
 buffer<decltype(function(source[0]))> target(source.size); target.apply(function, source); return target;
}

struct PlotView : HList<Plot> {
 unique<Window> window = ::window(this, int2(0, 720));
 bool shown = false;
 size_t index = 0;
 /*buffer<unique<FileWatcher>> watchers = apply2(arguments(), [this](string path){
  return unique<FileWatcher>(path, [this](string){ if(shown) load(); shown=false; window->render(); });
});*/
 PlotView() {
  window->actions[F12] = {this, &PlotView::snapshot};
  window->presentComplete = [this]{ shown=true; };
  load();
 }
 void snapshot() {
  String name = array<Plot>::at(0).ylabel+"-"+array<Plot>::at(0).xlabel;
  if(existsFile(name+".png"_)) log(name+".png exists");
  else {
   int2 size(1280, 720);
   Image target = render(size, graphics(vec2(size), Rect(vec2(size))));
   writeFile(name+".png", encodePNG(target), currentWorkingDirectory(), true);
  }
 }
 bool mouseEvent(vec2, vec2, Event, Button button, Widget*&) override {
  if(button == WheelUp || button == WheelDown) {
   index = (++index)%2;
   load();
   return true;
  }
  return false;
 }
 void load() {
  clear();
  Plot& plot = append();
  plot.xlabel = "Pressure (N)"__;
  plot.ylabel = "Stress (Pa)"__;
   map<String, array<Variant>> allCoordinates;
   for(Folder folder: arguments()) {
    for(string name: folder.list(Files)) {
     if(!endsWith(name,".result") && !endsWith(name,".working")) continue;
     auto parameters = parseDict(section(name,'.',0,-2));
     for(const auto parameter: parameters)
      if(!allCoordinates[::copy(parameter.key)].contains(parameter.value))
       allCoordinates.at(parameter.key).insertSorted(::copy(parameter.value));
    }
   }
   for(Folder folder: arguments()) {
    for(string name: folder.list(Files)) {
     if((!endsWith(name,".result") && !endsWith(name,".working")) || !existsFile(name, folder)) continue;
     TextData s (readFile(name, folder));
     s.until('\n'); // First line: constant results
     buffer<string> names = split(s.until('\n'),", "); // Second line: Headers
     map<string, array<float>> dataSets;
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
     auto parameters = parseDict(section(name,'.',0,-2));
     float pressure = parameters.at("Pressure");
     parameters.filter(
        [&](string key, const Variant&){ return key=="Pressure"_ || allCoordinates.at(key).size==1
        || key=="Speed"_ // FIXME
        ; }
     );
     auto& dataSet = plot.dataSets[str(parameters,", "_)];
     float maxStress = max(dataSets.at("Stress (Pa)"));
     dataSet.insertSorted(pressure, maxStress);
     //if(!dataSets.contains(plot.xlabel)) { continue; }
     //if(!dataSets.contains(plot.ylabel)) { continue; }
     /*plot.dataSets.insert(str(parameters,", "_),
          {::move(dataSets.at(plot.xlabel)), ::move(dataSets.at(plot.ylabel))});*/
    }
   }
   plot.min.x = 0, plot.max.x = 3.2e6;
   plot.min.y = 0, plot.max.y = 16e6;
   window->setTitle(array<Plot>::at(0).ylabel);
 }
} app;

