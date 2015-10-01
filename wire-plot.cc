#include "data.h"
#include "window.h"
#include "render.h"
#include "png.h"
#include "layout.h"
#include "plot.h"
#include "variant.h"
#include "pdf.h"

buffer<byte> toPDF(Widget& widget, vec2 pageSizeMM=210/*mm*/) {
 static constexpr float pointMM = 72 / 25.4;
 vec2 pageSize = pageSizeMM*pointMM;
 shared<Graphics> graphics = widget.graphics(pageSize, Rect(pageSize));
 graphics->flatten();
 return toPDF(pageSize, ref<Graphics>(graphics.pointer, 1), 1);
}

struct PlotView {
 HList<Plot> plots;
 unique<Window> window = nullptr;
 PlotView() {
  TextData s (readFile(arguments()[0]));
  float internodeLength = 2.47/2/1000; s.decimal(); s.until('\n');
  s.until('\n'); // Headers
  array<vec3> P;
  while(s) {
   float x = s.decimal(); s.whileAny(' ');
   float y = s.decimal(); s.whileAny(' ');
   float z = s.decimal(); s.whileAny(' ');
   P.append(vec3(x,y,z));
   s.until('\n');
  }
  array<float> X, Y;
  float x = 0;
  for(size_t i : range(P.size-1)) {
   vec3 A = P[i];
   vec3 B = P[i+1];
   float length = ::length(B-A);
   float y = length / internodeLength - 1;
   X.append(x);
   Y.append(y);
   //x += internodeLength;
   x += length;
  }

  const size_t N = X.size;

  double sum = 0;
  for(size_t i : range(N)) sum += Y[i];
  double mean = sum / N;

  double SSE = 0;
  for(size_t i : range(N)) SSE += sq(Y[i]-mean);
  double variance = SSE / N;

  if(1) {
   buffer<float> x (X.size), y (Y.size);
   for(size_t i : range(X.size)) {
    x[i] = X[i]*100/X.last();
    //y[i] = Y[i]*100;
    y[i] = (Y[i]-mean)/sqrt(variance)*100;
   }
   Plot plot;
   plot.xlabel = "Position (%)"__;
   plot.ylabel = "Tension (%)"__;
   plot.dataSets.insert(""__, {::move(x), ::move(y)});
   writeFile("wire-tension-"+arguments()[1]+".pdf"_, toPDF(plot, vec2(94.5)), home(), true);
   plots.append(::move(plot));
  }
  if(0) { // Verifies X[i] ~ i
   buffer<float> x (X.size), y (Y.size);
   for(size_t i : range(X.size)) {
    x[i] = i;
    y[i] = X[i]; //X[i+1]-X[i];
   }
   Plot plot;
   plot.ylabel = "Index"__;
   plot.ylabel = "Position (%)"__;
   plot.dataSets.insert(""__, {::move(x), ::move(y)});
   plots.append(::move(plot));
  }
  if(1) {
   // Autocorrelation
   // Assumes uniform stretch
   buffer<float> K (N), R (N);
   int firstMinimum = 0, globalMax = 0, globalMin = 0;
   for(size_t k : range(N)) {
    double r = 0;
    for(size_t i : range(N-k)) {
     r += (Y[i]-mean)*(Y[i+k]-mean);
     //r += (Y[i])*(Y[i+k]);
    }
    r /= N*variance;
    K[k] = (float) k * internodeLength * 100;
    R[k] = r;
    if(!firstMinimum && k && R[k]>R[k-1]) firstMinimum=k, globalMax=k; // First minimum -> Wait for next downward slope
    if(firstMinimum && R[k]>R[globalMax]) globalMax = k; // Global maximum (after first minimum)
    if(R[k]<R[globalMin]) globalMin = k; // Global maximum (after first minimum)
   }
   float xp = globalMax*internodeLength*100;
   float th = 3.6*0.02*100;
   log(firstMinimum, globalMax, globalMin, xp, th, xp-th, (xp-th)/th);
   Plot plot;
   plot.xlabel = "Lag (cm)"__;
   plot.ylabel = "Autocorrelation"__;
   plot.dataSets.insert(""__, {::copyRef(K.sliceRange(firstMinimum, globalMin)), ::copyRef(R.sliceRange(firstMinimum, globalMin))});
   writeFile("wire-tension-autocorrelation-"+arguments()[1]+".pdf"_, toPDF(plot, vec2(94.5)), home(), true);
   plots.append(::move(plot));
  }
  window = ::window(&plots, int2(720*plots.size, 720));
 }
} app;

