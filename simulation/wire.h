#pragma once
#include "system.h"

 struct Wire {
  sconst float radius = 2*mm;
  const float internodeLength;
  sconst float section = PI * sq(radius);
  const float volume = section * internodeLength;
  sconst float density = 2000 * kg / cb(m);
  const float mass = density * volume;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 50 * MPa;
  sconst float poissonRatio = 0.48;
  sconst float tensionStiffness = 1 * elasticModulus * PI * sq(radius);
  const float tensionDamping = 1/*?*/ * sqrt(mass * tensionStiffness);
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = 1 * elasticModulus * areaMomentOfInertia / internodeLength;
  const float bendDamping = 1 * mass / s;

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
  buffer<float> Fx { capacity };
  buffer<float> Fy { capacity };
  buffer<float> Fz { capacity };

  Wire(float internodeLength) : internodeLength(internodeLength), capacity(65536) {}

  const vec3 position(size_t i) const { return vec3(Px[i], Py[i], Pz[i]);  }
  const vec3 velocity(size_t i) const { return vec3(Vx[i], Vy[i], Vz[i]);  }
  const vec3 force(size_t i) const { return vec3(Fx[i], Fy[i], Fz[i]);  }
 };
