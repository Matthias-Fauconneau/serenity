#pragma once
typedef float __attribute((__vector_size__(16))) v4sf;

void side(int W, const v4sf* position, const v4sf* velocity, v4sf* force, float pressure, float internodeLength,
          float tensionStiffness, float tensionDamping, float radius, int start, int size);

#if __gcc__
__attribute__ ((target ("avx")))
void side(int W, const v4sf* position, v4sf* force,
          float pressure, float internodeLength, float tensionStiffness, int start, int size);
#endif
