#pragma once
#include "system.h"
#include "parallel.h"

// Sphere particles
struct Grain {
 virtual ~Grain() {}

 sconst float radius = 2.5 * mm; // 40 * mm
 sconst float density = 7.8e3 * kg / cb(m);
 sconst float volume = 4./3*PI*cb(radius);
 sconst float mass = volume * density; // 2.7 g
 sconst float curvature = 1./radius;
 sconst float poissonRatio = 0.28; // 0.35
 sconst float shearModulus = 77000 * MPa;
 sconst float elasticModulus = 2*shearModulus*(1+poissonRatio); // 250 * MPa
 sconst float angularMass = 2./3*mass*sq(radius);

 const size_t capacity;
 int count = 0;
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

 Grain(size_t capacity) : capacity(align(simd, capacity+simd)) {
  Px.clear(0); Py.clear(0); Pz.clear(0);
  Vx.clear(0); Vy.clear(0); Vz.clear(0);
  Fx.clear(0); Fy.clear(0); Fz.clear(0);
  Tx.clear(0); Ty.clear(0); Tz.clear(0);
  Rx.clear(0); Ry.clear(0); Rz.clear(0); Rw.clear(1);
 }

 const vec3 position(size_t i) const { return vec3(Px[simd+i], Py[simd+i], Pz[simd+i]);  }
 const vec3 velocity(size_t i) const { return vec3(Vx[simd+i], Vy[simd+i], Vz[simd+i]);  }
 const vec3 force(size_t i) const { return vec3(Fx[simd+i], Fy[simd+i], Fz[simd+i]);  }
 const vec4 rotation(size_t i) const { return vec4(Rx[simd+i], Ry[simd+i], Rz[simd+i], Rw[simd+i]);  }
};
