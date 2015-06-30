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
 PlotView() { load(); }
 void load() {
  plots.clear();
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
   Plot& plot = plots.append();
   plot.name = "Stress-strain curve"__;
   plot.xlabel = copyRef(ref<string>{"Displacement (mm)","Height (mm)"}[0]);
   plot.ylabel = "Force (N)"__;
   plot.dataSets.insert(str(parseDict(name.slice(0, name.size-".result"_.size))),
    {move(dataSets.at(plot.xlabel)), move(dataSets.at(plot.ylabel))});
  }
 }
} app;

