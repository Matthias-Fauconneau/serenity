#pragma once
#include "system.h"

// Plates
struct Plate {
 virtual ~Plate() {}
 sconst float mass = 1 * kg;
 sconst float curvature = 0;
 sconst float poissonRatio = 0.28;
 sconst float shearModulus = 77000 * MPa;
 sconst float elasticModulus = 2*shearModulus*(1+poissonRatio); // 180 * GPa
};
