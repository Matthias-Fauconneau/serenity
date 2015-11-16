#include "thread.h"
#include "window.h"
#include "png.h"
#include "layout.h"
#include "interface.h"
#include "snapshot-view.h"
#include "variant.h"
inline float length2(vec4f v) { return sqrt(sq2(v))[0]; }

struct Snapshot {
 ImageView image;
 unique<Window> window = ::window(&image, int2(512), mainThread, true);
 Snapshot() { // After window GL initialization
#define STATE "1%"_
  ref<string> arguments = {
  "Friction=0.3,Pattern=none,Pressure=80K,Radius=0.02,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ." STATE,
  "Elasticity=1e7,Friction=0.3,Pattern=helix,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%." STATE,
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=loop,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.2,Thickness=1e-3,TimeStep=10µ,Wire=12%." STATE,
  "Angle=3.6,Elasticity=1e7,Friction=0.3,Pattern=cross,Pressure=80K,Radius=0.02,Rate=400,Resolution=2,Seed=3,Side=1e8,Speed=0.1,Thickness=1e-3,TimeStep=10µ,Wire=12%." STATE
  };
  for(string id: arguments) {
   auto parameters = parseDict(id);
   log(parameters.at("Pattern"));
   SnapshotView sample(id);
   if(!id) return;
   if(1) {
    vec3 min = inf, max = -inf;
    float R = 0;
    for(vec4f O: sample.grain.position.slice(0, sample.grain.count)) {
     min = ::min(min, toVec3(O) - vec3(sample.grain.radius));
     max = ::max(max, toVec3(O) + vec3(sample.grain.radius));
     R = ::max(R, length2(toVec3(O))+sample.grain.radius);
    }
    const float height = max.z - min.z;
    const float volume = PI*sq(R)*height;
    const float grainVolume = sample.grain.count * 4./3 * PI * cb(sample.grain.radius);
    size_t wireCount = 0;
    for(v4sf p: sample.wire.position) {
     if(toVec3(p).z >= min.z && toVec3(p).z <= max.z && length2(p) <= R) wireCount++;
    }
    const float wireVolume = wireCount * PI * sq(sample.wire.radius) * sample.grain.radius/2;
    log(volume*1000, 100*grainVolume/volume, 100*wireVolume/volume, wireCount, sample.wire.radius, sample.grain.radius);
   }
   if(0) {
    if(existsFile(id+".grain")) copy(currentWorkingDirectory(), id+".grain", "/scratch"_, id+".grain");
    if(existsFile(id+".side")) copy(currentWorkingDirectory(), id+".side", "/scratch"_, id+".side");
    if(existsFile(id+".wire")) copy(currentWorkingDirectory(), id+".wire", "/scratch"_, id+".wire");
    if(existsFile(id+".grain-grain")) copy(currentWorkingDirectory(), id+".grain-grain", "/scratch"_, id+".grain-grain");
   }
   if(0) {
    int r = 1;
    if(existsFile(str(parameters.at("Pattern"))+"-"+str(r)+"K.png"_, "/scratch"_)) continue;
    extern float lineWidth; lineWidth = r;
    image = sample.render(r*1024);
    writeFile(str(parameters.at("Pattern"))+"-"+str(r)+"K.png"_, encodePNG(image.image), "/scratch"_);
   }
  }
  error("done");
 }
} app;
