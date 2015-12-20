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
 buffer<unique<FileWatcher>> watchers = apply2(/*arguments()*/ref<string>{"."}, [this](string path){
  return unique<FileWatcher>(path, [this](string){ if(shown) load(); shown=false; window->render(); });
});
 PlotView() {
  window->actions[Escape] = []{ exit_group(0); /*FIXME*/ };
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
   index++;
   index = index%2;
   load();
   return true;
  }
  return false;
 }
 void load() {
  clear();
  for(size_t index: range(2)) {
   Plot& plot = append();
   plot.xlabel = "Strain (%)"__;
   plot.ylabel = copyRef(ref<string>{"Axial (Pa)","Normalized deviator stress"}[index]);
   map<String, array<Variant>> allCoordinates;
   for(Folder folder: {"."_}/*arguments()*/) {
    for(string name: folder.list(Files)) {
     auto parameters = parseDict(name);
     for(const auto parameter: parameters)
      if(!allCoordinates[::copy(parameter.key)].contains(parameter.value))
       allCoordinates.at(parameter.key).insertSorted(::copy(parameter.value));
    }
    for(string name: folder.list(Files)) {
     TextData s (readFile(name, folder));
     s.until('\n'); // First line: constant results
     buffer<string> names = split(s.until('\n'),", "); // Second line: Headers
     map<string, array<float>> dataSets;
     for(string name: names) dataSets.insert(name);
     while(s) {
      for(size_t i = 0; s && !s.match('\n'); i++) {
       string d = s.whileDecimal();
       float decimal = parseDecimal(d);
       assert_(isNumber(decimal));
       assert_(i < dataSets.values.size, i, dataSets.keys);
       dataSets.values[i].append( decimal );
       s.whileAny(' ');
      }
     }
     //for(mref<float> data: dataSets.values) if(data) data[0]=0; // Filters invalid first point (inertia)
     auto parameters = parseDict(name);
     if(plot.ylabel == "Normalized deviator stress") {
      //float pressure = parameters.at("Pressure");
      ref<float> radial = dataSets.at("Radial (Pa)");
      ref<float> axial = dataSets.at("Axial (Pa)");
      array<float>& ndeviator = dataSets.insert("Normalized deviator stress");
      ndeviator.grow(axial.size);
      for(int i: range(ndeviator.size)) ndeviator[i] = ((axial[i]-radial[i])/radial[i]);
      //for(int i: range(ndeviator.size)) ndeviator[i] = ((axial[i]-min(pressure,radial[i]))/min(pressure,radial[i]));
     }
     assert_(dataSets.contains(plot.xlabel));
     assert_(dataSets.contains(plot.ylabel));
     parameters.filter([&](string key, const Variant&){ return allCoordinates.at(key).size==1; });
     plot.dataSets.insert(str(parameters,", "_),
     {::move(dataSets.at(plot.xlabel)), ::move(dataSets.at(plot.ylabel))});
    }
   }
  }
  if(count()) window->setTitle(array<Plot>::at(0).ylabel);
 }
} app;

