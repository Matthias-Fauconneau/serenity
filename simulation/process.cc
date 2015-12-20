#include "simulation.h"
#include "grain.h"
#include "membrane.h"

void Simulation::stepProcess() {
 // Process
 if(grain->count == targetGrainCount) {
  dynamicFrictionCoefficient = targetDynamicFrictionCoefficient;
  if(processState  < Pressure) { // Fits top plate while disabling gravity
   float topZ = 0;
   for(float z: grain->Pz.slice(simd, grain->count)) topZ = ::max(topZ, z+Grain::radius);
   if(topZ < this->topZ) this->topZ = topZ;
   topZ0 = this->topZ;
   Gz = 0;
  }
  if(processState < Pressure) {
   if(timeStep%(int(1/(dt*60))) == 0) log(maxGrainV*1e3f, "mm/s");
   if(maxGrainV > 600 * mm/s) return;
   pressure = targetPressure;
  }
  if(pressure < targetPressure || membraneViscosity < 1) { // Increases pressure toward target pressure
   processState = Pressure;
   //pressure += dt * targetPressure * Pa/s;
   membraneViscosity += 100 * dt * 1/s;
  } else { // Displace plates with constant velocity
   pressure = targetPressure;
   if(processState < Load) {
    topForceZ = 0; topSumStepCount = 0;
    bottomForceZ = 0; bottomSumStepCount = 0;
    radialForce = 0; radialSumStepCount = 0;
   }
   processState = Load;
   topZ -= dt * plateSpeed;
   bottomZ += dt * plateSpeed;
  }
 } else {
  // Increases current height
  if(currentHeight < topZ-Grain::radius) currentHeight += verticalSpeed * dt;

#if WIRE
  // Generates wire
  if(pattern) {
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
   float z = currentHeight+Grain::radius+Wire::radius; // Over pour
   //float z = currentHeight-Grain::radius-Wire::radius; // Under pour
   vec3 relativePosition = vec3(end.x, end.y, z) - wire.position(wire.count-1);
   float length = ::length(relativePosition);
   if(length >= Wire::internodeLength) {
    assert(wire.count < wire.capacity);
    vec3 p = wire.position(wire.count-1) + Wire::internodeLength * relativePosition/length;
    size_t i = wire.count++;
    wire.Px[i] = p[0]; wire.Py[i] = p[1]; wire.Pz[i] = p[2];
    wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
    // Forces verlet lists reevaluation
    grainWireGlobalMinD = 0;
   }
  }
#endif

  // Generates grain
  if(currentHeight >= Grain::radius) {
   for(;;) {
    if(grain->count == targetGrainCount) break;
    vec2 p(random()*2-1,random()*2-1);
    if(length(p)<1) { // Within cylinder
     vec3 newPosition ((membrane->radius-Grain::radius)*p.x, (membrane->radius-Grain::radius)*p.y, Grain::radius);
     if(grain->count) {// Deposits grain without grain overlap
      const/*expr*/ size_t threadCount = ::threadCount();
      float maxZ_[threadCount]; mref<float>(maxZ_, threadCount).clear(0);
      processTime += parallel_chunk(align(simd, grain->count)/simd,
                                    [this, newPosition, &maxZ_](uint id, size_t start, size_t size) {
       float* const pPx = grain->Px.begin()+simd, *pPy = grain->Py.begin()+simd, *pPz = grain->Pz.begin()+simd;
       const vXsf nPx = floatX(newPosition.x), nPy = floatX(newPosition.y);
       const vXsf _2_Gr2 = floatX(sq(2*Grain::radius));
       vXsf maxZ = _0f;
       for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
        vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
        vXsf Rx = Px-nPx, Ry = Py-nPy;
        vXsf Dxy2 = Rx*Rx + Ry*Ry;
        vXsf Dz = sqrt(_2_Gr2 - Dxy2);
        maxZ = max(Pz+Dz, maxZ); // Dxy2>_2Gr2: max(NaN, z) = z
       }
       maxZ_[id] = max(maxZ_[id], max(maxZ));
      });
      for(size_t k: range(threadCount)) newPosition.z = ::max(newPosition.z, maxZ_[k]);
      // for(size_t k: range(threadCount)) log(k, maxZ_[k]);
      //log(newPosition.z);
     }
     // Under current wire drop height
     if(newPosition.z < currentHeight) {
#if WIRE
      // Without wire overlap
      for(size_t index: range(wire.count))
       if(length(wire.position(index) - newPosition) < Grain::radius+Wire::radius) { processTime.stop(); return; }
#endif
      size_t i = grain->count;
      assert_(newPosition.z >= Grain::radius);
      grain->Px[simd+i] = newPosition.x; grain->Py[i+simd] = newPosition.y; grain->Pz[simd+i] = newPosition.z;
      grain->Vx[simd+i] = 0; grain->Vy[simd+i] = 0; grain->Vz[simd+i] = 0; //- 1 * m/s;
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
       float grainVolume = grain->count * Grain::volume;
       voidRatio = (totalVolume-grainVolume)/grainVolume;
      }
      // Forces verlet lists reevaluation
      /*if(timeStep%grain->count == 0)*/ grainMembraneGlobalMinD = 0;
      grainGrainGlobalMinD = 0;
      grainWireGlobalMinD = 0;
     } else break;
    }
    break;
   }
  }
 }
}
