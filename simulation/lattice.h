#pragma once
#include "memory.h"

generic struct Lattice {
 const vec3 scale;
 const vec3 min, max;
 const int3 size = ::max(int3(5,5,1), int3(::floor(scale*(max-min)))+int3(2));
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.y*size.x+size.x+1+ size.z*size.y*size.x + 2*size.y*size.x+2*size.x+2)}; // -1 .. 2 OOB margins
 const mref<T> base = cells.slice(size.y*size.x+size.x+1);
 Lattice(float scale, vec3 min, vec3 max) : scale(scale), min(min), max(max) {
  assert_(size.x < (1<<10) && size.y < (1<<10) && size.z < (1<<10));
  assert_(cells.size < (1<<30));
  cells.clear(-1);
 }
 inline int index(float x, float y, float z) {
  /*int index
    = int(scale.z*(z-min.z)) * (size.y*size.x)
    + int(scale.y*(y-min.y)) * size.x
    + int(scale.x*(x-min.x));*/
  const vXsf Ax = floatX(x), Ay = floatX(y), Az = floatX(z);
  const vXsf scaleX = floatX(scale.x), scaleY = floatX(scale.y), scaleZ = floatX(scale.z);
  const vXsf minX = floatX(min.x), minY = floatX(min.y), minZ = floatX(min.z);
  const vXsi sizeX = intX(size.x), sizeYX = intX(size.y * size.x);
  vXsi indexX = convert(scaleZ*(Az-minZ)) * sizeYX
                     + convert(scaleY*(Ay-minY)) * sizeX
                     + convert(scaleX*(Ax-minX));
  //assert_(indexX[0] == index, indexX[0], index);
  int index = indexX[0];
  assert(index >= 0 && index < size.z*size.y*size.x, index);
  return index;
 }
 inline T& cell(float x, float y, float z) { return base[index(x, y, z)]; }
};
