#pragma once
#include "system.h"
#include "parallel.h"

// Sphere particles
struct Grain {
 const float density;
 const float shearModulus;
 const float poissonRatio;
 const float elasticModulus = 2*shearModulus*(1+poissonRatio);

 const float radius;
 const float curvature = 1./radius;
 const float volume;
 const float mass;
 const float angularMass = 2./3*mass*sq(radius); // Thin shell

 const size_t capacity;
 int count = 0;
 buffer<float> Px { capacity };
 buffer<float> Py { capacity };
 buffer<float> Pz { capacity };
#if GEAR
 buffer<float> PDx[2] {capacity, capacity};
 buffer<float> PDy[2] {capacity, capacity};
 buffer<float> PDz[2] {capacity, capacity};
#endif
 buffer<float> Vx { capacity };
 buffer<float> Vy { capacity };
 buffer<float> Vz { capacity };
 buffer<float> Fx { ::threadCount() * capacity };
 buffer<float> Fy { ::threadCount() * capacity };
 buffer<float> Fz { ::threadCount() * capacity };
 buffer<float> Rx { capacity }, Ry { capacity }, Rz { capacity }, Rw { capacity };
 buffer<float> AVx { capacity }, AVy { capacity }, AVz { capacity }; // Angular velocity
 buffer<float> Tx { ::threadCount() * capacity };
 buffer<float> Ty { ::threadCount() * capacity };
 buffer<float> Tz { ::threadCount() * capacity }; // Torque

 Grain(float radius, float density, float shearModulus, float poissonRatio, float thickness, size_t capacity)
  : density(density), shearModulus(shearModulus), poissonRatio(poissonRatio),
    radius(radius), volume(4./3*PI*cb(radius)), mass(density*4*PI*sq(radius)*(thickness?:radius/3)),
    capacity(align(simd, capacity+simd)) {
  Px.clear(0); Py.clear(0); Pz.clear(0);
  Vx.clear(0); Vy.clear(0); Vz.clear(0);
  Fx.clear(0); Fy.clear(0); Fz.clear(0);
  Tx.clear(0); Ty.clear(0); Tz.clear(0);
  Rx.clear(0); Ry.clear(0); Rz.clear(0); Rw.clear(1);
 }
 virtual ~Grain() {}

 const vec3 position(size_t i) const { return vec3(Px[simd+i], Py[simd+i], Pz[simd+i]);  }
 const vec3 velocity(size_t i) const { return vec3(Vx[simd+i], Vy[simd+i], Vz[simd+i]);  }
 const vec3 force(size_t i) const { return vec3(Fx[simd+i], Fy[simd+i], Fz[simd+i]);  }
 const vec4 rotation(size_t i) const { return vec4(Rx[simd+i], Ry[simd+i], Rz[simd+i], Rw[simd+i]);  }
};
