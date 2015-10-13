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
  if(0) {
   vec2 end;
   double length = 0;
   double winchAngle = 0;
   double loopAngle = 3.6, winchRadius = 0.02; // -wireRadius
   while(winchAngle < loopAngle) {
    double A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    double R = winchRadius, r = R * loopAngle / (2*PI);
    vec2 nextEnd = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    length += ::length(nextEnd-end);
    end = nextEnd;
    winchAngle += R/r * 1e-3;
   }
   //error(winchAngle, length*100);
  }
  ref<string> arguments = {
  //"Friction=0.3,Pattern=none,Pressure=80K,Radius=0.02,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ.10%",
  "Elasticity=1e7,Friction=0.3,Pattern=helix,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%",
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=loop,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.2,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%",
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=cross,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%"
  };
  for(string id: arguments) {
   auto parameters = parseDict(id);
   log(parameters.at("Pattern"));
   TextData s (readFile(id+".wire"_));
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
   //double mean = 0;
   double mean = sum / N;

   double SSE = 0;
   for(size_t i : range(N)) SSE += sq(Y[i]-mean);
   double variance = SSE / N;

   if(1) {
    buffer<float> x (X.size), y (Y.size);
    for(size_t i : range(X.size)) {
     x[i] = X[i]*100/X.last();
     y[i] = Y[i]*100;
     //y[i] = (Y[i]-mean)/sqrt(variance)*100;
     if(y[i] < 0) y[i] = 0;
    }
    Plot plot;
    plot.xlabel = "Position along wire length"__; //from start to end (%)
    plot.ylabel = "Elongation (%)"__;
    /*plot.min.y = 0;
    plot.max.y = ::max(y);*/
    plot.dataSets.insert(""__, {::move(x), ::move(y)});
    writeFile("wire-tension-"+str(parameters.at("Pattern"))+".pdf"_, toPDF(plot, vec2(94.5, 94.5/1.5)), home(), true);
    plots.append(::move(plot));
   }
   /*if(0) { // Verifies X[i] ~ i
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
   }*/
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
     if(K[k] >= 30) break;
     R[k] = r *100;
     if(R[k] < 0) break;
     //if(R[k] >= 40) firstMinimum = k;
     if(!firstMinimum && k && R[k]>R[k-1]) firstMinimum=k, globalMax=k; // First minimum -> Wait for next downward slope
     if(firstMinimum && R[k]>R[globalMax]) globalMax = k; // Global maximum (after first minimum)
     if(R[k]<R[globalMin]) globalMin = k; // Global maximum (after first minimum)
    }
    //float xp = globalMax*internodeLength*100;
    //float th = length*100; //3.6*0.02*100;
    //log(/*firstMinimum, globalMax, globalMin, xp, th, xp-th, (xp-th)/th*/ xp, th,  (xp-th)/th*100, R[globalMax]);
    Plot plot;
    //plot.min.x = 5; plot.max.x = 30;
    plot.xlabel = "Lag (cm)"__; //plot.xlabel = "Offset (cm)"__;
    plot.ylabel = "Autocorrelation (%)"__;
    /*if(id==arguments[0])*/ firstMinimum=0;
    plot.dataSets.insert(""__, {::copyRef(K.sliceRange(firstMinimum, globalMin)), ::copyRef(R.sliceRange(firstMinimum, globalMin))});
    /*plot.min.y = 0;
    plot.max.y = 40;*/
    /*plot.min.x = 0;
    plot.max.x = 30;*/
    writeFile("wire-tension-autocorrelation-"+str(parameters.at("Pattern"))+".pdf"_, toPDF(plot, vec2(94.5, 94.5/1.5)), home(), true);
    plots.append(::move(plot));
   }
  }
  window = ::window(&plots, int2(720*plots.size, 720));
 }
} app;


