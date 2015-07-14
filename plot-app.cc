#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "variant.h"

struct PlotView : HList<Plot> {
 unique<Window> window = ::window(this, int2(0, 720));
 bool shown = false;
 size_t index = 0;
 FileWatcher watcher{".", [this](string){ if(shown) load(); shown=false; window->render(); } };
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
  for(size_t unused i : range(0,1)) {
   Plot& plot = append();
   plot.xlabel = "Strain (%)"__;
   plot.ylabel = unsafeRef(ref<string>{"Normalized Deviatoric Stress","Stress (Pa)"}[index]);
   map<String, array<Variant>> allCoordinates;
   for(string name: currentWorkingDirectory().list(Files)) {
    if(!endsWith(name,".result")) continue;
    auto parameters = parseDict(name.slice(0, name.size-".result"_.size));
    for(const auto parameter: parameters)
     if(!allCoordinates[::copy(parameter.key)].contains(parameter.value))
      allCoordinates.at(parameter.key).insertSorted(::copy(parameter.value));
   }
   for(string name: currentWorkingDirectory().list(Files)) {
    if(!endsWith(name,".result") || !existsFile(name)) continue;
    TextData s (readFile(name));
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
    if(!dataSets.contains(plot.xlabel)) { /*log("Missing data", plot.xlabel);*/ continue; }
    if(!dataSets.contains(plot.ylabel)) { /*log("Missing data", plot.ylabel);*/ continue; }
    auto parameters = parseDict(name.slice(0, name.size-".result"_.size));
    parameters.filter(
       [&](string key, const Variant&){ return allCoordinates.at(key).size==1; }
    );
    plot.dataSets.insert(str(parameters,", "_),
    {::move(dataSets.at(plot.xlabel)), ::move(dataSets.at(plot.ylabel))});
   }
  }
  window->setTitle(array<Plot>::at(0).ylabel);
 }
} app;

