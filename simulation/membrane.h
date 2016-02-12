#pragma once
#include "system.h"

struct Membrane {
 virtual ~Membrane() {}

 sconst float density = 1000 * kg / cb(m);
 sconst float curvature = 0;
 sconst float shearModulus = 0.1/*3*/ * MPa;
 sconst float poissonRatio = 0.48;
 sconst float elasticModulus = 2*shearModulus*(1+poissonRatio);

 const float radius;
 const float resolution;//= 2*PI*radius/64;
 const int W = int(round(2*PI*radius/resolution))/simd*simd;
 const int margin = simd; // 16 to ensure no false sharing ?
 const int stride = margin+W+margin;
 const float internodeLength = 2*sin(PI/W)*radius;
 const float ratio;
 const float exactHeight = radius * ratio;
 const float cellHeight = sqrt(3.)/2*internodeLength;
 const int H = ceil(exactHeight/cellHeight)+1;
 const float height = (H-1) * cellHeight;
 sconst float thickness = 0.05 * mm;
 const float tensileStrength = 1680/*1-1680*/ * MPa;
 const float mass = sqrt(3.)/2 * sq(internodeLength) * thickness * density;
 const float tensionStiffness = sqrt(3.)/2 * internodeLength * thickness * tensileStrength;
 const float tensionDamping = 1 * sqrt(mass * tensionStiffness);
 //sconst float areaMomentOfInertia = pow4(1*mm); // FIXME
 //const float bendStiffness = 0;//elasticModulus * areaMomentOfInertia / internodeLength; // FIXME

 const size_t capacity = H*stride;
 const size_t count = capacity;
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

 Membrane(float radius, float resolution, float ratio) : radius(radius), resolution(resolution), ratio(ratio) {
  Px.clear(0); Py.clear(0); Pz.clear(0);
  Vx.clear(0); Vy.clear(0); Vz.clear(0);
  Fx.clear(0); Fy.clear(0); Fz.clear(0);
#if GEAR
  PDx[0].clear(0); PDy[0].clear(0); PDz[0].clear(0);
  PDx[1].clear(0); PDy[1].clear(0); PDz[1].clear(0);
#endif
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
};
