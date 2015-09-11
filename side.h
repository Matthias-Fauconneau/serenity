#pragma once
typedef unsigned short uint16;
typedef float __attribute((__vector_size__(16))) v4sf;

void side(int W, const v4sf* position, const v4sf* velocity, v4sf* force, float pressure,
          float internodeLength, float tensionStiffness, float tensionDamping, float radius,
          int start, int size
          //, const uint16* grainBase, int gX, int gY, v4sf scale, v4sf min
          );
