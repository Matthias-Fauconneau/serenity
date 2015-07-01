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
 FileWatcher watcher{".", [this](string){ load(); window->render(); } };
 PlotView() { window->actions[F12] = {this, &PlotView::snapshot}; load(); }
 void snapshot() {
  string name = "plot";
  if(existsFile(name+".png"_)) log(name+".png exists");
  else {
   int2 size(1280, 720);
   Image target = render(size, plots.graphics(vec2(size), Rect(vec2(size))));
   writeFile(name+".png", encodePNG(target), currentWorkingDirectory(), true);
  }
 }
 void load() {
  plots.clear();
  for(size_t i : range(1,2)) {
   Plot& plot = plots.append();
   plot.xlabel = copyRef(ref<string>{"Displacement (mm)","Height (mm)"}[i]);
   plot.ylabel = "Force (N)"__;
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
      float decimal = s.decimal();
      assert_(isNumber(decimal));
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

