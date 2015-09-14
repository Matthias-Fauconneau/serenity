#pragma once
typedef unsigned short uint16;
typedef float __attribute((__vector_size__(16))) v4sf;

void side(int W, const v4sf* position, v4sf* force, float pressure,
          float internodeLength, float tensionStiffness, float radius,
          int start, int size);
