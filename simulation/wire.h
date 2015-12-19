#pragma once
#include "system.h"

#if WIRE
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
#endif
