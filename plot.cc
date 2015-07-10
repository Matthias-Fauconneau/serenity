#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "variant.h"

struct PlotView {
 HList<Plot> plots;
 unique<Window> window = ::window(&plots, int2(0, 720));
 bool shown = false;
 FileWatcher watcher{".", [this](string){ if(shown) load(); shown=false; window->render(); } };
 PlotView() {
  window->actions[F12] = {this, &PlotView::snapshot};
  window->presentComplete = [this]{ shown=true; };
  load();
 }
 void snapshot() {
  String name = plots[0].ylabel+"-"+plots[0].xlabel;
  if(existsFile(name+".png"_)) log(name+".png exists");
  else {
   int2 size(1280, 720);
   Image target = render(size, plots.graphics(vec2(size), Rect(vec2(size))));
   writeFile(name+".png", encodePNG(target), currentWorkingDirectory(), true);
  }
 }
 void load() {
  plots.clear();
  for(size_t unused i : range(0,1)) {
   Plot& plot = plots.append();
   plot.xlabel = "Strain (%)"__; //copyRef(ref<string>{"Displacement (mm)","Height (mm)"}[i]);
   plot.ylabel = "Normalized Deviatoric Stress"__;
   //plot.ylabel = "Stress (Pa)"__;
   //plot.ylabel = "Offset (N)"__;
   //"Force (N)"__;
   map<String, array<Variant>> allCoordinates;
   for(string name: currentWorkingDirectory().list(Files)) {
    if(!endsWith(name,".result")) continue;
    auto parameters = parseDict(name.slice(0, name.size-".result"_.size));
    for(const auto parameter: parameters)
     if(!allCoordinates[copy(parameter.key)].contains(parameter.value))
      allCoordinates.at(parameter.key).insertSorted(copy(parameter.value));
   }
   for(string name: currentWorkingDirectory().list(Files)) {
    if(!endsWith(name,".result")) continue;
    TextData s (readFile(name));
    s.until('\n'); // First line: constant results
    buffer<string> names = split(s.until('\n'),", "); // Second line: Headers
    map<string, array<float>> dataSets;
    for(string name: names) dataSets.insert(name);
    while(s) {
     for(size_t i = 0; s && !s.match('\n'); i++) {
      string d = s.whileDecimal();
      assert_(d, s.slice(s.index-16,16),"|", s.slice(s.index));
      float decimal = parseDecimal(d);
      assert_(isNumber(decimal), s.slice(s.index-16,16),"|", s.slice(s.index));
      assert_(i < dataSets.values.size, i, dataSets);
      dataSets.values[i].append( decimal );
      s.whileAny(' ');
     }
    }
    if(!dataSets.contains(plot.xlabel)) { /*log("Missing data", plot.xlabel);*/ continue; }
    if(!dataSets.contains(plot.ylabel)) { /*log("Missing data", plot.ylabel);*/ continue; }
    auto parameters = parseDict(name.slice(0, name.size-".result"_.size));
    parameters.filter(
       [&](string key, const Variant&){ return allCoordinates.at(key).size==1; }
    );
    plot.dataSets.insert(str(parameters,", "_),
    {move(dataSets.at(plot.xlabel)), move(dataSets.at(plot.ylabel))});
   }
  }
 }
} app;

