#include "file.h"
#include "jpeg.h"
#include "png.h"
#include "thread.h"

struct ratio { uint64 num, den; };
inline ratio SSE(const Image& rA, const Image& rB, int2 offset=0, uint64 minCount=0) {
 const Image* pA = &rA;
 const Image* pB = &rB;
 if(pB->size < pA->size) { swap(pA, pB); offset = -offset; }
 const Image& A = *pA;
 const Image& B = *pB;
 assert_(A.size <= B.size, A.size, B.size);
 offset += (B.size - A.size)/2;
 int2 size = min(A.size-max(int2(0), offset), min(B.size, offset+A.size) - offset);
 uint64 count = size.x*size.y;
 if(count < minCount) return {uint64(-1), 0};
 assert_(size <= A.size);
 assert_(offset+size <= B.size);
 assert_(size.x && size.y, size, A.size, B.size, offset);
 uint64 energy = 0;
 for(size_t y: range(size.y)) for(size_t x: range(size.x)) {
  int2 p = int2(x,y);
  int2 a = p + max(int2(0), -offset);
  int2 b = p + max(int2(0),  offset);
  energy += sq(bgr3i(A(a.x, a.y).bgr()) - bgr3i(B(b.x, b.y).bgr())); // SSE
 }
 return {energy, count};
}

struct Compare {
 Compare() {
  //{const int N = 1080; for(int d: range(1, N)) if(N%d==0) log(d, N/d, int2(4,3)*d);}
  array<String> files = Folder(".").list(Files|Sorted);
  ref<int> scales {30, 36};
  array<array<Image>> images;
  for(int N: scales) {
   array<String> loFiles = Folder(str(N), currentWorkingDirectory(), true).list(Files|Sorted);
   for(string file : files) {
    if(!loFiles.contains(file)) {
     Image image = decodeJPEG(Map(file));
     log(image.size);
     assert_(image.size.x % N == 0 && image.size.y % N == 0);
     Image lo (image.size/int(N));
     int2 offset = (image.size-(image.size/int(N))*int(N))/2;
     for(size_t y: range(lo.size.y)) {
      for(size_t x: range(lo.size.x)) {
       bgr3i sum = 0;
       for(size_t dy: range(N)) {
        for(size_t dx: range(N)) {
         sum += bgr3i(image(offset.x+x*N+dx, offset.y+y*N+dy).bgr());
        }
       }
       lo(x, y) = byte4(byte3(sum / int(N*N)));
      }
     }
     writeFile(file, encodePNG(lo), str(N));
    }
   }
   images.append(apply(files, [N](string file) { Image image = decodePNG(Map(file, str(N))); return copy(cropRef(image, 0, image.size/int2(1,2))); })); // Cuts foreground
  }

  //struct Transform { int2 offset = 0; struct Ratio { int num, div; } scale = {0,0}; } bestTransform;
  struct Transform { int2 offset = 0; struct { int A, B; } scale = {0,0}; };
  struct Similarity { Transform transform; float similarity; ratio ratio; };
  auto compare = [scales, &images](size_t a, size_t b) {
   double bestSSE = inf;
   ratio bestRatio = {uint64(-1),0};
   Transform bestTransform = {int2(0), {0,0}};
   for(int scaleA: range(scales.size)) {
    for(int scaleB: range(scales.size)) {
     for(int y: range(-0, 0 +1)) {
      for(int x: range(-0, 0 +1)) {
       Transform transform {int2(x, y), {scaleA, scaleB}};
       assert_(a < images[transform.scale.A].size, a, transform.scale.A, images[transform.scale.A]);
       assert_(b < images[transform.scale.B].size, b);
       ratio ratio = ::SSE(images[transform.scale.A][a], images[transform.scale.B][b], transform.offset, 7776);
       double SSE = double(ratio.num) / double(ratio.den);
       if(SSE < bestSSE) {
        bestRatio = ratio;
        bestSSE = SSE;
        bestTransform = transform;
       }
      }
     }
    }
   }
   float similarity = 1-sqrt(bestSSE)/(3*256);
   return Similarity{bestTransform, similarity, bestRatio};
  };

  ImageF similarityMatrix (arguments().size);
  float minTrueSimilarity = inf;
  for(size_t i: range(similarityMatrix.size.x)) {
   similarityMatrix(i, i) = 1;
   for(size_t j: range(i)) {
    float similarity = compare(files.indexOf(arguments()[i]), files.indexOf(arguments()[j])).similarity;
    similarityMatrix(i, j) = similarityMatrix(j, i) = similarity;
    minTrueSimilarity = ::min(minTrueSimilarity, similarity);
   }
  }
  float minSimilarity = inf, maxSimilarity = 0;
  for(size_t a: range(images[0].size)) {
   if(!arguments().contains(files[a])) {
    for(size_t i: range(similarityMatrix.size.x)) {
     size_t b = files.indexOf(arguments()[i]);
     auto S = compare(a, b);
     minSimilarity = ::min(minSimilarity, S.similarity);
     maxSimilarity = ::max(maxSimilarity, S.similarity);
     assert_(100*(S.similarity/minTrueSimilarity-1) < 3, minTrueSimilarity, 100*(S.similarity/minTrueSimilarity-1));
    }
   }
  }
  for(size_t a: range(images[0].size)) {
   for(size_t i: range(similarityMatrix.size.x)) {
    size_t b = files.indexOf(arguments()[i]);
    auto S = compare(a, b);
    log(files[a].slice(5,3), files[b].slice(5,3), S.transform.scale.A, S.transform.scale.B, S.transform.offset, round(100*(S.similarity-minSimilarity)/(maxSimilarity-minSimilarity))); //, S.ratio.num, S.ratio.den);
   }
  }
 }
} app;
