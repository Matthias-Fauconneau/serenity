#pragma once
#include "memory.h"

generic struct Lattice {
 const vec3 scale;
 const vec3 min, max;
 const int3 size = ::max(int3(5,5,1), int3(::floor(toVec3(scale*(max-min))))+int3(2));
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.z*size.y*size.x + 3*size.y*size.x+3*size.x+4)}; // -1 .. 2 OOB margins
 T* const base = cells.begin()+size.y*size.x+size.x+1;
 Lattice(float scale, vec3 min, vec3 max) : scale(scale), min(min), max(max) {
  cells.clear(0);
 }
 inline size_t index(float x, float y, float z) {
  int index
    = int(scale.z*(z-min.z)) * (size.y*size.x)
    + int(scale.y*(y-min.y)) * size.x
    + int(scale.x*(x-min.x));
  assert(index >= 0 && index < size.z*size.y*size.x, index, min, x,y,z, max);
  return index;
 }
 inline T& cell(float x, float y, float z) { return base[index(x, y, z)]; }
};

template<size_t cellCapacity> struct List : mref<uint16> {
 List(mref<uint16> o) : mref(o) {}
 bool append(uint16 index) {
  assert(index != 0);
  size_t i = 0;
  while(at(i)) { i++; assert(i<cellCapacity, (mref<uint16>)*this); }
  at(i) = index; i++;
  assert(i < cellCapacity, index, i, cellCapacity, (mref<uint16>)*this);
  at(i) = 0;
  return true;
 }
};
