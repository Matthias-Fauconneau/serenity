#include "simulation.h"
//#include "process.h"
//#include "grain-grain.h"
//#include "grain-wire.h"

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep")), radius(p.at("Radius")),
  pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
 }
}

bool Simulation::domain(vec3& min, vec3& max) {
 min = inf, max = -inf;
 // Grain
 // Avoids bound checks on grain-wire
 for(size_t i: range(grain.count)) {
  min.x = ::min(min.x, grain.Px[i]);
  max.x = ::max(max.x, grain.Px[i]);
  min.y = ::min(min.y, grain.Py[i]);
  max.y = ::max(max.y, grain.Py[i]);
  min.z = ::min(min.z, grain.Pz[i]);
  max.z = ::max(max.z, grain.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain grain", min, max);
  processState = ProcessState::Error;
  return false;
 }
 // Wire
 // Avoids bound checks on grain-wire
 for(size_t i: range(wire.count)) {
  min.x = ::min(min.x, wire.Px[i]);
  max.x = ::max(max.x, wire.Px[i]);
  min.y = ::min(min.y, wire.Py[i]);
  max.y = ::max(max.y, wire.Py[i]);
  min.z = ::min(min.z, wire.Pz[i]);
  max.z = ::max(max.z, wire.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain wire", min, max);
  processState = ProcessState::Error;
  return false;
 }
 return true;
}

unique<Lattice<uint16> > Simulation::generateLattice(const Mass& vertices, float radius) {
 vec3 min, max;
 if(!domain(min, max)) return nullptr;
 unique<Lattice<uint16>> lattice(sqrt(3.)/(2*radius), min, max);
 for(size_t i: range(vertices.count))
  lattice->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]) = 1+i;
 return move(lattice);
}

unique<Grid> Simulation::generateGrid(const Mass& vertices, float length) {
 vec3 min, max;
 if(!domain(min, max)) return nullptr;
 unique<Grid> grid(1/length, min, max);
 for(size_t i: range(vertices.count))
  grid->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]).append(1+i);
 return move(grid);
}

bool Simulation::step() {

 stepProcess();

 stepGrain();
 stepGrainGrain();

 stepGrainWire();

 stepWire();

 // Grain
 float maxGrainV = 0;
 for(size_t i: range(grain.count)) { // TODO: SIMD
  System::step(grain, i);
  maxGrainV = ::max(maxGrainV, length(grain.velocity(i)));
 }
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD6 -= maxGrainGrainV * dt;

 // Wire
 for(size_t i: range(wire.count)) { // TODO: SIMD
  System::step(wire, i);
 }

 timeStep++;
 return true;
}

void Simulation::stepGrain() {
 // Grain
 buffer<int> grainBottom(grain.count, 0), grainSide(grain.count, 0);
 for(size_t a: range(grain.count)) {
  // Initialization
  grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
  grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
  // Contact with obstacles (floor, side)
  if(grain.Pz[a]-Grain::radius < 0) grainBottom.append( a );
  if(processState == ProcessState::Pour &&
     sq(grain.Px[a]) + sq(grain.Py[a]) > sq(radius - Grain::radius)) {
   grainSide.append( a );
  }
 }
 {// Contact force with floor (SIMD)
  buffer<float> Fx(grainBottom.size), Fy(grainBottom.size), Fz(grainBottom.size);
  for(size_t i = 0; i < grainBottom.size; i += 8) {
   v8si A = *(v8si*)&grainBottom[i];
   v8sf depth = float8(Grain::radius) - gather(grain.Pz, A);
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   contact<Grain, Obstacle>(grain, A, depth,
                            _0f, _0f, float8(-Grain::radius),
                            _0f, _0f, _1f,
                            Ax, Ay, Az,

                            *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
  }
  for(size_t i = 0; i < grainBottom.size; i++) { // Scalar scatter add
   size_t a = grainBottom[i];
   assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
   grain.Fx[a] += Fx[i];
   grain.Fy[a] += Fy[i];
   grain.Fz[a] += Fz[i];
  }
 }
 float sideForce = 0;
 if(processState <= ProcessState::Pour) { // Rigid Side
  size_t gSC = grainRigidSideCount;
  while(gSC%8) grainRigidSide[gSC++] = grain.count;
  v8sf R = float8(radius-Grain::radius);
  buffer<float> Fx(gSC), Fy(gSC), Fz(gSC);
  for(size_t i = 0; i < gSC; i += 8) {
   v8si A = *(v8si*)&grainRigidSide[i];
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A);
   v8sf length = sqrt8(Ax*Ax + Ay*Ay);
   v8sf Nx = -Ax/length, Ny = -Ay/length;
   v8sf depth = length - R;
   contact<Grain, RigidSide>(grain, A, depth, Grain::radius8*(-Nx), Grain::radius8*(-Ny), _0f,
                             Nx, Ny, _0f,
                             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
  }
  for(size_t i = 0; i < grainRigidSideCount; i++) { // Scalar scatter add
   size_t a = grainRigidSide[i];
   assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
   grain.Fx[a] += Fx[i];
   grain.Fy[a] += Fy[i];
   grain.Fz[a] += Fz[i];
   sideForce += (grain.Px[a]*Fx[i] + grain.Py[a]*Fy[i]) / sqrt(grain.Px[a]*grain.Px[a] + grain.Py[a]*grain.Py[a]); // dot(R^,F)
  }
 }
}


void Simulation::stepWire() {
 buffer<int> wireBottom(wire.count, 0);

 for(size_t i: range(wire.count)) { // TODO: SIMD
  wire.Fx[i] = 0; wire.Fy[i] = 0; wire.Fz[i] = wire.mass * Gz;
  if(wire.Pz[i]-Wire::radius < 0) wireBottom.append( i );
 }

 {// Bottom
  buffer<float> Fx(wireBottom.size), Fy(wireBottom.size), Fz(wireBottom.size);
  for(size_t i = 0; i < wireBottom.size; i += 8) {
   v8si A = *(v8si*)&wireBottom[i];
   v8sf depth = float8(Wire::radius) - gather(wire.Pz, A);
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   contact(wire, A, depth,
           _0f, _0f, _1f,
           Ax, Ay, Az,
           *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
  }
  for(size_t i = 0; i < wireBottomCount; i++) { // Scalar scatter add
   size_t a = wireBottom[i];
   wire.Fx[a] += Fx[i];
   wire.Fy[a] += Fy[i];
   wire.Fz[a] += Fz[i];
  }
 }

 // Tension
 for(size_t i: range(1, wire.count)) { // TODO: SIMD
  size_t a = i-1, b = i;
  vec3 relativePosition = wire.position(a) - wire.position(b);
  vec3 length = ::length(relativePosition);
  vec3 x = length - wire.internodeLength;
  vec3 fS = - wire.tensionStiffness * x;
  vec3 direction = relativePosition/length;
  vec3 relativeVelocity = wire.velocity(a) - A.velocity(b);
  vec3 fB = - wire.tensionDamping * dot(direction, relativeVelocity);
  vec3 fT = (fS + fB) * direction;
  wire.Fx[a] += fT.x;
  wire.Fy[a] += fT.y;
  wire.Fz[a] += fT.z;
  wire.Fx[b] -= fT.x;
  wire.Fy[b] -= fT.y;
  wire.Fz[b] -= fT.z;
 }
}

