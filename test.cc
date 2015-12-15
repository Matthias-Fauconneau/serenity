#include "simd.h"

struct Test {
 Test() {
  atomic contactCount;
  uint GGcc = 8;
  buffer<int> grainGrainA = buffer<int>(GGcc, 0);
  buffer<int> grainGrainB = buffer<int>(GGcc, 0);
  int* const ggA = grainGrainA.begin(), *ggB = grainGrainB.begin();
  maskX mask = _seqi < intX(4);
  log(str(moveMask(mask), 8u, '0', 2u));
  uint targetIndex = contactCount.fetchAdd(countBits(mask));
  compressStore(ggA+targetIndex, mask, intX(0)+_seqi);
 }
};
