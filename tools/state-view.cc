#include "state-view.h"
#include "window.h"

struct StateViewApp {
 Map map;
 StateView view;
 bool running = true;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);

 StateViewApp(string file) : map(file) {
  load();
  window->presentComplete = [this]() {
   if(!running) return;
   const float dt = 10e-6; // FIXME
   const float Dt = 1./60;
   view.timeStep += Dt/dt;
   if(load()) window->render();
  };
 }
 bool load() {
  BinaryData data(map);
  size_t t = 0;
  for(;;) {
   if(!data) { log("EOF"); running=false; return false; }
   view.state.grain.count = data.read32();
   view.state.grain.radius = data.readF();
   view.state.membrane.count = data.read64();
   view.state.membrane.W = data.read32();
   view.state.membrane.H = data.read32();
   view.state.membrane.stride = data.read32();
   view.state.membrane.margin = data.read32();
   view.state.membrane.radius = data.readF();
   data.index = align(64, data.index);
   if(t == view.timeStep) break;
   data.index += 19*align(64, view.state.grain.count)*sizeof(float);
   data.index += 9*align(64, view.state.membrane.count)*sizeof(float);
   t++;
  }
  {
   auto read = [&](ref<float>& A) {
    A = data.read<float>(align(64, view.state.grain.count));
    A.size = view.state.grain.count;
   };
#define VSG view.state.grain
   read(VSG.Px); read(VSG.Py); read(VSG.Pz);
   read(VSG.Vx); read(VSG.Vy); read(VSG.Vz);
   read(VSG.Fx); read(VSG.Fy); read(VSG.Fz);
   read(VSG.Rx); read(VSG.Ry); read(VSG.Rz); read(VSG.Rw);
   read(VSG.AVx); read(VSG.AVy); read(VSG.AVz);
   read(VSG.Tx); read(VSG.Tx); read(VSG.Tx);
  }
  {
   auto read = [&](ref<float>& A) {
    A = data.read<float>(align(64, view.state.membrane.count));
    A.size = view.state.membrane.count;
   };
#define VSM view.state.membrane
   read(VSM.Px); read(VSM.Py); read(VSM.Pz);
   read(VSM.Vx); read(VSM.Vy); read(VSM.Vz);
   read(VSM.Fx); read(VSM.Fy); read(VSM.Fz);
  }
  return true;
 }
} app ( replace(arguments()[0],"/",":")+".dump" );
