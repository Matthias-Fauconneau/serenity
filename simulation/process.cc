#include "simulation.h"
#include "grain.h"
#include "membrane.h"
#include "wire.h"

void Simulation::stepProcess() {
 //pressure = 0; membraneViscosity = 1-1000*dt; // DEBUG
 // Process
 if(grain->count == targetGrainCount) {
  if(!useMembrane) return;
  if(processState  < Pressure) {
   static bool unused once = ({ log("Set Friction"); true; });
   dynamicGrainObstacleFrictionCoefficient = targetDynamicGrainObstacleFrictionCoefficient;
   dynamicGrainMembraneFrictionCoefficient = targetDynamicGrainMembraneFrictionCoefficient;
   dynamicGrainGrainFrictionCoefficient = targetDynamicGrainGrainFrictionCoefficient;
   dynamicWireGrainFrictionCoefficient = targetDynamicWireGrainFrictionCoefficient;
   dynamicWireBottomFrictionCoefficient = targetDynamicWireBottomFrictionCoefficient;
   staticFrictionSpeed = targetStaticFrictionSpeed;
   staticFrictionLength = targetStaticFrictionLength;
   staticFrictionStiffness = targetStaticFrictionStiffness;
   staticFrictionDamping = targetStaticFrictionDamping;
  }
  if(processState < Load) { // Fits top plate while disabling gravity
   static bool unused once = ({ log("Fits plate"); true; });
   float topZ = 0;
   for(float z: grain->Pz.slice(simd, grain->count)) topZ = ::max(topZ, z+grain->radius);
   if(topZ < this->topZ) this->topZ = topZ;
   topZ0 = this->topZ;
   //log("Zeroes gravity");
   Gz = 0;
  }
  /*if(processState < Pressure) {
   static bool unused once = ({ log("Enables pressure"); true; });
   //if(timeStep%(int(1/(dt*60/s))) == 0) log(maxGrainV*1e3f, "mm/s");
   //if(maxGrainV > 600 * mm/s) return;
   pressure = targetPressure;
  }*/
  const float targetViscosity = 1-10000*dt;
  if(pressure < targetPressure || membraneViscosity < targetViscosity) { // Increases pressure toward target pressure
   if(processState < Pressure) log("Release");
   processState = Pressure;
   if(pressure < targetPressure) pressure += 100 * targetPressure * dt;
   if(pressure > targetPressure) pressure = targetPressure;
   //pressure=targetPressure; //assert_(pressure== targetPressure, pressure, targetPressure);
   if(membraneViscosity < targetViscosity) membraneViscosity += 100 * dt;
   if(membraneViscosity > targetViscosity) membraneViscosity = targetViscosity;
  } else { // Displaces plates with constant velocity
   pressure = targetPressure;
   if(processState < Load) {
    topForceZ = 0; topSumStepCount = 0;
    bottomForceZ = 0; bottomSumStepCount = 0;
    radialForce = 0; radialSumStepCount = 0;
    //log("Displaces plates");
   }
   processState = Load;
   topZ -= dt * plateSpeed;
   bottomZ += dt * plateSpeed;
  }
 } else {
  // Increases current height
  if(currentHeight < topZ-grain->radius/*/2*/) currentHeight += verticalSpeed * dt;
  //else currentHeight = topZ-grain->radius;

  // Generates wire
  if(pattern && currentHeight < topZ-grain->radius) {
   vec2 end;
   if(pattern == Helix) { // Simple helix
    float a = winchAngle;
    float r = patternRadius;
    end = vec2(r*cos(a), r*sin(a));
    winchAngle += linearSpeed/patternRadius * dt;
   }
   else if(pattern == Cross) { // Cross reinforced helix
    if(currentWinchRadius < -patternRadius) { // Radial -> Tangential (Phase reset)
     currentWinchRadius = patternRadius;
     lastAngle = winchAngle+PI;
     winchAngle = lastAngle;
    }
    if(winchAngle < lastAngle+loopAngle) { // Tangential phase
     float a = winchAngle;
     float r = patternRadius;
     end = vec2(r*cos(a), r*sin(a));
     winchAngle += linearSpeed/patternRadius * dt;
    } else { // Radial phase
     float a = winchAngle;
     float r = currentWinchRadius;
     end = vec2(r*cos(a), r*sin(a));
     currentWinchRadius -= linearSpeed * dt; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    float R = patternRadius, r = R * loopAngle / (2*PI);
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += linearSpeed/r * dt;
   } else error("Unknown pattern:", int(pattern));
   float z = currentHeight+grain->radius+Wire::radius; // Over pour
   //float z = currentHeight-grain->radius-Wire::radius; // Under pour
   vec3 relativePosition = vec3(end.x, end.y, z) - wire->position(wire->count-1);
   float length = ::length(relativePosition);
   if(length >= wire->internodeLength) {
    assert(wire->count < (int)wire->capacity);
    vec3 newPosition = wire->position(wire->count-1) + wire->internodeLength/length * relativePosition;
    // Without grain overlap
    for(size_t index: range(grain->count))
     if(::length(grain->position(index) - newPosition) < grain->radius+Wire::radius) {
      //log("Grain blocks wire trajectory");
      return;
     }
    size_t i = wire->count++;
    wire->Px[i] = newPosition[0]; wire->Py[i] = newPosition[1]; wire->Pz[i] = newPosition[2];
    wire->Vx[i] = 0; wire->Vy[i] = 0; wire->Vz[i] = 0;
    // Forces verlet lists reevaluation
    wireGrainGlobalMinD = 0;
   }
  }

  // Generates grain
  if(currentHeight >= grain->radius && 1) {
   for(;;) {
    if(grain->count == targetGrainCount) break;
    vec2 p(random()*2-1,random()*2-1);
    if(length(p)<1) { // Within unit circle
     const float inradius = membrane->radius*cos(PI/membrane->W) /*/2*//*DEBUG*/ -grain->radius;
     vec3 newPosition (inradius*p.x, inradius*p.y, grain->radius);
     if(grain->count) {// Deposits grain without grain overlap
      const/*expr*/ size_t threadCount = ::threadCount();
      float maxTZ_[threadCount]; mref<float>(maxTZ_, threadCount).clear(0);
      float* const maxTZ = maxTZ_;
      processTime += parallel_chunk(align(simd, grain->count)/simd,
                                    [this, newPosition, maxTZ](uint id, size_t start, size_t size) {
       float* const pPx = grain->Px.begin()+simd, *pPy = grain->Py.begin()+simd, *pPz = grain->Pz.begin()+simd;
       const vXsf nPx = floatX(newPosition.x), nPy = floatX(newPosition.y);
       const vXsf _2_Gr2 = floatX(sq(2*grain->radius));
       vXsf maxZ = _0f;
       for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
        vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
        vXsf Rx = Px-nPx, Ry = Py-nPy;
        vXsf Dxy2 = Rx*Rx + Ry*Ry;
        vXsf Dz = sqrt(_2_Gr2 - Dxy2);
        maxZ = max(Pz+Dz, maxZ); // Dxy2>_2Gr2: max(NaN, z) = z
       }
       maxTZ[id] = max(maxTZ[id], max(maxZ));
      });
      for(size_t k: range(threadCount)) newPosition.z = ::max(newPosition.z, maxTZ[k]);
      // for(size_t k: range(threadCount)) log(k, maxTZ[k]);
      //log(newPosition.z);
     }
     // Under current wire drop height
     if(newPosition.z < currentHeight) {
      // Without wire overlap
      for(size_t index: range(wire->count))
       if(length(wire->position(index) - newPosition) < grain->radius+Wire::radius) return;

      size_t i = grain->count;
      assert_(newPosition.z >= grain->radius);
      grain->Px[simd+i] = newPosition.x;
      grain->Py[simd+i] = newPosition.y;
      grain->Pz[simd+i] = newPosition.z;
      grain->Vx[simd+i] = 0; grain->Vy[simd+i] = 0;
      //grain->Vz[simd+i] = -1 * m/s;
      grain->Vz[simd+i] = 0 * m/s;
      grain->AVx[simd+i] = 0; grain->AVy[simd+i] = 0; grain->AVz[simd+i] = 0;
      float t0 = 2*PI*random();
      float t1 = acos(1-2*random());
      float t2 = (PI*random()+acos(random()))/2;
      grain->Rx[simd+i] = sin(t0)*sin(t1)*sin(t2);
      grain->Ry[simd+i] = cos(t0)*sin(t1)*sin(t2);
      grain->Rz[simd+i] = cos(t1)*sin(t2);
      grain->Rw[simd+i] = cos(t2);
      //log(i, grain->Pz[simd+i]);
      grain->count++;
      {
       float height = topZ-bottomZ;
       float totalVolume = PI*sq(membrane->radius)*height;
       float grainVolume = grain->count * grain->volume;
       voidRatio = (totalVolume-grainVolume)/grainVolume;
      }
      // Forces verlet lists reevaluation
      grainMembraneGlobalMinD = 0;
      grainGrainGlobalMinD = 0;
      wireGrainGlobalMinD = 0;
      assert_(!validGrainLattice);
     } else break;
    }
    break;
   }
  }
 }
}
