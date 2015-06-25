#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"

struct PlotView {
 HList<Plot> plots;
 unique<Window> window = ::window(&plots, int2(720));
 PlotView() {
  array<array<float>> dataSets;
  TextData s (readFile(arguments()[0]));
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
  for(size_t i: range(1, dataSets.size)) {
   Plot& plot = plots.append();
   auto& dataSet = plot.dataSets.insert(copyRef(names[i]));
   auto& values = dataSets[i];
   dataSet.reserve(values.size);
   for(size_t i: range(values.size)) dataSet[dataSets[0][i]] = values[i];
  }
 }
} app;

