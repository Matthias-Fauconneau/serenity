#pragma once
#include "memory.h"

generic struct Lattice {
 const float scale;
 const vec3 min, max;
 const int3 size = ::max(int3(5,5,1), int3(::floor(scale*(max-min)))+int3(2));
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.y*size.x+size.x+1+ size.z*size.y*size.x + 2*size.y*size.x+2*size.x+2)}; // -1 .. 2 OOB margins
 const mref<T> base = cells.slice(size.y*size.x+size.x+1);
 Lattice(float scale, vec3 min, vec3 max) : scale(scale), min(min), max(max) {
  assert_(size.x < (1<<10) && size.y < (1<<10) && size.z < (1<<10));
  assert_(cells.size < (1<<30));
 }
};
