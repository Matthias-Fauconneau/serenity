#include "snapshot-view.h"
#include "window.h"

struct StateViewApp {
 Map map;
 size_t timeStep = 0;
 StateView view;

 unique<Window> window = ::window(&view, -1, mainThread, true, false);

 StateViewApp(string file) : map(file) { load(); }
 void load() {
  BinaryData data(map);
  view.state.grain.count = data.read32();
  //view.state.grain.radius = (float&)data.read32();
  view.state.membrane.count = data.read64();
  view.state.membrane.W = data.read32();
  view.state.membrane.H = data.read32();
  view.state.membrane.stride = data.read32();
  view.state.membrane.margin = data.read32();
  view.state.membrane.radius = (const float&)data.read32();
  view.state.grain.Px = data.read<float>(view.state.grain.count);
  view.state.grain.Py = data.read<float>(view.state.grain.count);
  view.state.grain.Pz = data.read<float>(view.state.grain.count);
  view.state.grain.Vx = data.read<float>(view.state.grain.count);
  view.state.grain.Vy = data.read<float>(view.state.grain.count);
  view.state.grain.Vz = data.read<float>(view.state.grain.count);
  view.state.grain.Fx = data.read<float>(view.state.grain.count);
  view.state.grain.Fy = data.read<float>(view.state.grain.count);
  view.state.grain.Fz = data.read<float>(view.state.grain.count);
  view.state.grain.Rx = data.read<float>(view.state.grain.count);
  view.state.grain.Ry = data.read<float>(view.state.grain.count);
  view.state.grain.Rz = data.read<float>(view.state.grain.count);
  view.state.grain.Rw = data.read<float>(view.state.grain.count);
  view.state.grain.AVx = data.read<float>(view.state.grain.count);
  view.state.grain.AVy = data.read<float>(view.state.grain.count);
  view.state.grain.AVz = data.read<float>(view.state.grain.count);
  view.state.grain.Tx = data.read<float>(view.state.grain.count);
  view.state.grain.Tx = data.read<float>(view.state.grain.count);
  view.state.grain.Tx = data.read<float>(view.state.grain.count);
  view.state.membrane.Px = data.read<float>(view.state.membrane.count);
  view.state.membrane.Py = data.read<float>(view.state.membrane.count);
  view.state.membrane.Pz = data.read<float>(view.state.membrane.count);
  view.state.membrane.Vx = data.read<float>(view.state.membrane.count);
  view.state.membrane.Vy = data.read<float>(view.state.membrane.count);
  view.state.membrane.Vz = data.read<float>(view.state.membrane.count);
  view.state.membrane.Fx = data.read<float>(view.state.membrane.count);
  view.state.membrane.Fy = data.read<float>(view.state.membrane.count);
  view.state.membrane.Fz = data.read<float>(view.state.membrane.count);
 }
} app ( replace(arguments()[0],"/",":")+".dump" );
