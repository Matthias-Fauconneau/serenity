#pragma once
#include "memory.h"
#include "vector.h"
#include "simd.h"
#include "parallel.h"
#define sconst static constexpr
static inline constexpr float pow4(float x) { return x*x*x*x; }
template<> inline String str(const vXsf& a) { return str(ref<float>((float*)&a, simd)); }
template<> inline String str(const vXsi& a) { return str(ref<int>((int*)&a, simd)); }

/// Evolving system of objects and obstacles interacting through contacts (SoA)
struct System {
 // Units
 const float dt;
 sconst float s = 1, m = 1, kg = 1, N = kg /m /(s*s), Pa = N / (m*m);
 sconst float mm = 1e-3*m, g = 1e-3*kg, KPa = 1e3 * Pa, MPa = 1e6 * Pa;

 // Contact parameters
 //sconst float e = 1./2; // Restitution coefficient
 //const float normalDampingRate = ln(e) / sqrt(sq(PI)+ln(ln(e)));
 sconst float normalDampingRate = 1; // ~ ln e / √(π²+ln²e) [restitution coefficient e]
 sconst float dynamicFrictionCoefficient = 0;//0.1; // 1
 sconst float staticFrictionSpeed = 0;//__builtin_inff();
 sconst float staticFrictionLength = 0;//3 * mm; // ~ Wire::radius
 sconst float staticFrictionStiffness = 0;//100 * 1*g*10/(1*mm); //k/F = F/L ~ Wire::mass*G/Wire::radius
 sconst float staticFrictionDamping = 0;//0.01 * kg/s; // TODO: relative to k ?

 // Obstacles: floor plane, cast cylinder
 struct Obstacle {
  sconst float mass = 1 * kg;
  sconst float curvature = 0;
  sconst float elasticModulus = 100 * MPa;
  sconst float poissonRatio = 0;
 };

 // Sphere particles
 struct Grain {
  sconst bool validation = true;
  sconst float radius = validation ? 2.47 * mm: 40 * mm;
  sconst float density = 7.6e3 * kg/cb(m);
  sconst float mass = validation ? 4./3*PI*cb(radius) * density /*~5g*/ : 2.7 * g;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 100 * MPa; // 1e3
  sconst float poissonRatio = 0.35;
  sconst float angularMass = 2./3*mass*sq(radius);

  const size_t capacity;
  size_t count = 0;
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
  buffer<float> Vx { capacity };
  buffer<float> Vy { capacity };
  buffer<float> Vz { capacity };
  buffer<float> Fx { ::threadCount() * capacity };
  buffer<float> Fy { ::threadCount() * capacity };
  buffer<float> Fz { ::threadCount() * capacity };

   // TODO: Rodrigues vector
  buffer<float> Rx { capacity }, Ry { capacity }, Rz { capacity }, Rw { capacity };
  buffer<float> AVx { capacity }, AVy { capacity }, AVz { capacity }; // Angular velocity
  buffer<float> Tx { ::threadCount() * capacity };
  buffer<float> Ty { ::threadCount() * capacity };
  buffer<float> Tz { ::threadCount() * capacity }; // Torque

  Grain() : capacity(4*3840/8+simd) {
   Px.clear(0); Py.clear(0); Pz.clear(0);
   Vx.clear(0); Vy.clear(0); Vz.clear(0);
   Fx.clear(0); Fy.clear(0); Fz.clear(0);
   Tx.clear(0); Ty.clear(0); Tz.clear(0);
   Rx.clear(0); Ry.clear(0); Rz.clear(0); Rw.clear(1);
  }

  const vec3 position(size_t i) const { return vec3(Px[simd+i], Py[simd+i], Pz[simd+i]);  }
  const vec3 velocity(size_t i) const { return vec3(Vx[i], Vy[i], Vz[i]);  }
  const vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]);  }
  const vec4 rotation(size_t i) const { return vec4(Rx[i], Ry[i], Rz[i], Rw[i]);  }
 } grain;

 struct Wire {
  sconst float radius = 3*mm;
  sconst float internodeLength = Grain::radius/2;
  sconst float section = PI * sq(radius);
  sconst float volume = section * internodeLength;
  sconst float density = 1000 * kg / cb(m);
  sconst float mass = Wire::density * Wire::volume;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1e0 * MPa;
  sconst float poissonRatio = 0.48;
  sconst float tensionStiffness = elasticModulus * PI * sq(radius);
  const float tensionDamping = 2 * sqrt(mass * tensionStiffness);
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  sconst float bendStiffness = 0; //elasticModulus * areaMomentOfInertia / internodeLength;
  sconst float bendDamping = 0*g /*mass*/ / s;

  const size_t capacity;
  size_t count = 0;
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
  buffer<float> Vx { capacity };
  buffer<float> Vy { capacity };
  buffer<float> Vz { capacity };
  buffer<float> Fx { capacity };
  buffer<float> Fy { capacity };
  buffer<float> Fz { capacity };

  Wire() : capacity(0) {}

  const vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]);  }
  const vec3 velocity(size_t i) const { return vec3(Vx[i], Vy[i], Vz[i]);  }
  const vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]);  }
 } wire;

 struct Membrane {
  sconst float density = 1000 * kg / cb(m);
  sconst float curvature = 0;
  sconst float elasticModulus = 100 * MPa;
  sconst float poissonRatio = 0.48;

  sconst float resolution = Grain::radius / 2;
  const float radius;
  const int W = int(2*PI*radius/resolution)/simd*simd;
  const int margin = simd; // 16 to ensure no false sharing ?
  const int stride = margin+W+margin;
  const float internodeLength = 2*sin(PI/W)*radius;
  const float exactHeight = radius * 4;
  const float cellHeight = sqrt(3.)/2*internodeLength;
  const int H = ceil(exactHeight/cellHeight)+1;
  const float height = (H-1) * cellHeight;
  sconst float thickness = 1 * mm;
  const float tensionElasticModulus = 100 * MPa; // 22-100
  const float mass = sqrt(3.)/2 * sq(internodeLength) * thickness * density;
  const float tensionStiffness = sqrt(3.)/2 * internodeLength * thickness * tensionElasticModulus;
  const float tensionDamping = 2 * sqrt(mass * tensionStiffness);
  //sconst float areaMomentOfInertia = pow4(1*mm); // FIXME
  //const float bendStiffness = 0;//elasticModulus * areaMomentOfInertia / internodeLength; // FIXME

  const size_t capacity = H*stride;
  const size_t count = capacity;
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
  buffer<float> Vx { capacity };
  buffer<float> Vy { capacity };
  buffer<float> Vz { capacity };
  buffer<float> Fx { capacity };
  buffer<float> Fy { capacity };
  buffer<float> Fz { capacity };

  Membrane(float radius) : radius(radius) {
   Px.clear(0); Py.clear(0); Pz.clear(0); // TODO: Gear?
   Vx.clear(0); Vy.clear(0); Vz.clear(0); // TODO: Gear?
   Fx.clear(0); Fy.clear(0); Fz.clear(0); // TODO: Gear?
   for(size_t i: range(H)) {
    for(size_t j: range(W)) {
     float z = i*height/(H-1);
     float a = 2*PI*(j+(i%2)*1./2)/W;
     float x = radius*cos(a), y = radius*sin(a);
     Px[i*stride+margin+j] = x;
     Py[i*stride+margin+j] = y;
     Pz[i*stride+margin+j] = z;
    }
    // Copies position back to repeated nodes
    Px[i*stride+margin-1] = Px[i*stride+margin+W-1];
    Py[i*stride+margin-1] = Py[i*stride+margin+W-1];
    Pz[i*stride+margin-1] = Pz[i*stride+margin+W-1];
    Px[i*stride+margin+W] = Px[i*stride+margin+0];
    Py[i*stride+margin+W] = Py[i*stride+margin+0];
    Pz[i*stride+margin+W] = Pz[i*stride+margin+0];
   }
  }

  const vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]);  }
  const vec3 velocity(size_t i) const { return vec3(Vx[i], Vy[i], Vz[i]);  }
  const vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]);  }
 } membrane;

 size_t timeStep = 0;

 System(float dt, float radius) : dt(dt), membrane(radius) {}
};
