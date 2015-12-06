#pragma once
#include "memory.h"

generic struct Lattice {
 const vec3 scale;
 const vec3 min, max;
 const int3 size = ::max(int3(5,5,1), int3(::floor(scale*(max-min)))+int3(2));
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.z*size.y*size.x + 3*size.y*size.x+3*size.x+4)}; // -1 .. 2 OOB margins
 const mref<T> base = cells.slice(size.y*size.x+size.x+1);
 Lattice(float scale, vec3 min, vec3 max) : scale(scale), min(min), max(max) {
  cells.clear(0);
 }
 inline size_t index(float x, float y, float z) {
  int index
    = int(scale.z*(z-min.z)) * (size.y*size.x)
    + int(scale.y*(y-min.y)) * size.x
    + int(scale.x*(x-min.x));
  assert(index >= 0 && index < size.z*size.y*size.x, index);
  return index;
 }
 inline T& cell(float x, float y, float z) { return base[index(x, y, z)]; }
};
