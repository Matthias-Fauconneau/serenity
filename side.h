#pragma once
typedef unsigned short uint16;
typedef float __attribute((__vector_size__(16))) v4sf;

#define CYLINDERGRID 1
#define GRAINGRID 0
#if !CYLINDERGRID && GRAINGRID
#define SINGLEGRID 1
#endif

void side(int W, const v4sf* position, const v4sf* velocity, v4sf* force, float pressure, float internodeLength,
          float tensionStiffness, float tensionDamping, float radius, int start, int size
          #if SINGLEGRID
          , const uint16* grainBase, int gX, int gY, v4sf scale, v4sf min
          #endif
          );

#if __gcc__
__attribute__ ((target ("avx")))
void side(int W, const v4sf* position, v4sf* force,
          float pressure, float internodeLength, float tensionStiffness, int start, int size);
#endif
