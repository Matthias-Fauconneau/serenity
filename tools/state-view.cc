#include "state-view.h"
#include "window.h"

struct StateViewApp {
 Map map;
 StateView view;
 bool running = true;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);

 StateViewApp(string file) : map(file) {
  window->backgroundColor = black;
  view.maximum = load(-1).timeStep;
  window->presentComplete = {this, &StateViewApp::next};
  window->actions[Space] = [this]() { running = !running; next(); };
  window->actions[LeftArrow] = [this]() { show(max(0, int(view.state.timeStep)-1)); };
  window->actions[RightArrow] = [this]() { show(min(view.maximum, view.state.timeStep+1)); };
  view.valueChanged = {this, &StateViewApp::show};
 }
 void next() {
  if(!running) return;
  const float dt = 10e-6; // FIXME
  const float Dt = 1./60;
  view.value += Dt/dt;
  if(view.value >= view.maximum) running = false;
  show(view.value);
  if(view.state.maxF > 30000/*N*/) running = false;
 }
 void show(size_t timeStep) {
  view.state = load(timeStep);
  window->render();
  window->setTitle(str(view.state.maxF)+"N");
 }
 State load(size_t position) {
  BinaryData data(map);
  State state;
  while(data) {
   state.grain.count = data.read32();
   state.grain.radius = data.readF();
   state.membrane.count = data.read32();
   state.membrane.W = data.read32();
   state.membrane.H = data.read32();
   state.membrane.stride = data.read32();
   state.membrane.margin = data.read32();
   state.membrane.radius = data.readF();
   size_t forceCount = 0; //data.read32();
   data.index = align(64, data.index);
   {
    auto read = [state, &data](ref<float>& A) {
     A = data.read<float>(align(64, state.grain.count));
     A.size = state.grain.count;
    };
    read(state.grain.Px); read(state.grain.Py); read(state.grain.Pz);
    //read(state.grain.Vx); read(state.grain.Vy); read(state.grain.Vz);
    //read(state.grain.Fx); read(state.grain.Fy); read(state.grain.Fz);
    read(state.grain.Rx); read(state.grain.Ry); read(state.grain.Rz); read(state.grain.Rw);
    //read(state.grain.AVx); read(state.grain.AVy); read(state.grain.AVz);
    //read(state.grain.Tx); read(state.grain.Tx); read(state.grain.Tx);
   }
   {
    auto read = [state, &data](ref<float>& A) {
     A = data.read<float>(align(64, state.membrane.count));
     A.size = state.membrane.count;
    };
    read(state.membrane.Px); read(state.membrane.Py); read(state.membrane.Pz);
    //read(state.membrane.Vx); read(state.membrane.Vy); read(state.membrane.Vz);
    read(state.membrane.Fx); read(state.membrane.Fy); read(state.membrane.Fz);
   }
   state.forces = data.read<State::Force>(forceCount);
   data.index = align(64, data.index);
   if(state.timeStep == position) {
    float maxF = 0;
    /*if(state.membrane.H) {
     for(size_t i=1; i<state.membrane.H-1; i++) {
      int base = state.membrane.margin+i*state.membrane.stride;
      for(size_t j=0; j<state.membrane.W; j++) {
       int k = base+j;
       maxF = ::max(maxF, length(state.membrane.force(k)));
      }
     }
    }*/
    for(size_t i: range(state.forces.size))
     if(isNumber(length(state.forces[i].force)))
      maxF = ::max(maxF, length(state.forces[i].force));
    state.maxF = maxF;
    break;
   }
   state.timeStep++;
  }
  return state;
 }
} app ( replace(arguments()[0],"/",":")+".dump" );
