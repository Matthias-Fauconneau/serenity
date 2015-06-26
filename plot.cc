#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"

struct PlotView {
 HList<Plot> plots;
 unique<Window> window = ::window(&plots, int2(0, 720));
 PlotView() {
  for(string name: currentWorkingDirectory().list(Files)) {
   if(!endsWith(name,".result")) continue;
   array<array<float>> dataSets;
   TextData s (readFile(name));
   s.until('\n'); // First line: grain.count wire.count wireDensity
   while(s) {
    for(size_t i = 0; s && !s.match('\n'); i++) {
     float decimal = s.decimal();
     assert_(isNumber(decimal));
     if(i>=dataSets.size) dataSets.append();
     dataSets[i].append( decimal );
     s.whileAny(' ');
    }
   }
   static ref<string> names {"Load", "Height", "Tension energy", "stretch"};
   assert_(names.size == dataSets.size, dataSets.size);
   for(size_t i: range(1/*dataSets.size-1*/)) {
    if(i>=plots.size) {
     plots.append();
     plots[i].name = copyRef(names[i+1]);
    }
    auto& dataSet = plots[i].dataSets.insert(copyRef(name));
    auto& values = dataSets[i+1];
    dataSet.reserve(values.size);
    for(size_t i: range(values.size)) {
     log(dataSets[0][i]);
     dataSet[dataSets[0][i]] = values[i];
    }
   }
  }
 }
} app;

