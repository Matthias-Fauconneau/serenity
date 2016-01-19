#pragma once
#include "system.h"

 struct Wire {
  sconst float radius = 3*mm;
  const float internodeLength;
  sconst float section = PI * sq(radius);
  const float volume = section * internodeLength;
  sconst float density = 1000 * kg / cb(m);
  const float mass = density * volume;
  sconst float curvature = 1./radius;
  sconst float elasticModulus = 1e0 * MPa;
  sconst float poissonRatio = 0.48;
  sconst float tensionStiffness = 1 * elasticModulus * PI * sq(radius);
  const float tensionDamping = 2 * sqrt(mass * tensionStiffness);
  sconst float areaMomentOfInertia = PI/4*pow4(radius);
  const float bendStiffness = elasticModulus * areaMomentOfInertia / internodeLength;
  const float bendDamping = 0.1 * mass / s;

  const size_t capacity;
  int count = 0;
  buffer<float> Px { capacity };
  buffer<float> Py { capacity };
  buffer<float> Pz { capacity };
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
