#include "thread.h"
#include "window.h"
#include "png.h"
#include "layout.h"
#include "interface.h"
#include "snapshot-view.h"
#include "variant.h"

struct Snapshot {
 ImageView image;
 unique<Window> window = ::window(&image, int2(512), mainThread, true);
 Snapshot() { // After window GL initialization
  ref<string> arguments = {
  "Friction=0.3,Pattern=none,Pressure=80K,Radius=0.02,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ.10%",
  "Elasticity=1e7,Friction=0.3,Pattern=helix,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%",
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=loop,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.2,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%",
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=cross,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%.10%"
  };
  for(string name: arguments) {
   auto parameters = parseDict(name);
   log(parameters.at("Pattern"));
   int r = 4;
   if(existsFile(str(parameters.at("Pattern"))+"-"+str(r)+"K.png"_, "/scratch"_)) continue;
   extern int lineWidth;
   lineWidth = r;
   image = SnapshotView(name).render(r*1024);
   writeFile(str(parameters.at("Pattern"))+"-"+str(r)+"K.png"_, encodePNG(image.image), "/scratch"_);
  }
  error("done");
 }
} app;
