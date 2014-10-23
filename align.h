#pragma once
#include "transform.h"

/// Evaluates residual energy between A and transform B
static double SSE(const ImageF& A, const ImageF& B, Transform transform) {
	int2 offset = int2(round(transform.offset*vec2(B.size)));
	return SSE(A, B, offset);
}

#define MIPMAP 0
#if MIPMAP
/// Evaluates first \a levelCount mipmap levels (shares source as first element)
static array<ImageF> mipmap(const ImageF& source, int levelCount) {
	array<ImageF> mipmap; mipmap.append( share(source) );
	for(int unused level: range(levelCount)) mipmap.append(downsample(mipmap.last()));
	return mipmap;
}
#endif

/// Aligns images
struct Align : ImageTransformGroupOperation, OperationT<Align> {
	string name() const override { return "[align]"; }

	// Evaluates residual energy at integer offsets
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		for(auto& image: images) assert_(image.size == images[0].size);
		const int levelCount = log2(uint(images[0].size.x/8)); // / (32, 24)
		array<Transform> transforms;
		transforms.append();
#if MIPMAP
		array<ImageF> A = mipmap(images[0], levelCount);
		for(const ImageF& image : images.slice(1)) { // Compares each image with first one (TODO: full regression)
			array<ImageF> B = mipmap(image, levelCount);
			Transform bestTransform {vec2(0,0)};
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				const ImageF& a = A[levelCount-1-level];
				const ImageF& b = B[levelCount-1-level];
				Transform levelBestTransform = bestTransform;
				double bestEnergy = SSE(a, b, levelBestTransform);
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					for(int2 offset: { int2(0,-1), int2(-1,0),int2(1,0), int2(0,1) }) { // Evaluate single steps along each translation axis
						Transform transform = levelBestTransform * Transform{vec2(offset)/vec2(b.size)};
						double energy = SSE(a, b, transform);
						if(energy < bestEnergy) {
							bestEnergy = energy;
							stepBestTransform = transform;
						}
					}
					if(stepBestTransform==levelBestTransform) break;
					levelBestTransform = stepBestTransform;
					//break;
				}
				if(levelBestTransform != Transform{vec2(0,0)}) {
					// Asserts higher level walk was not as large as too miss a better shorter alignment at lower levels
					double originalEnergy = SSE(a, b, Transform{vec2(0,0)});
					assert_(bestEnergy <= originalEnergy, bestEnergy, originalEnergy, bestTransform);
				}
				bestTransform = levelBestTransform;
			}
			transforms.append(bestTransform);
		}
#else
		const ImageF& A = images[0];
		for(const ImageF& B : images.slice(1)) { // Compares each image with first one (TODO: full regression)
			Transform bestTransform {vec2(0,0)};
			double bestEnergy = SSE(A, B, bestTransform);
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				Transform levelBestTransform = bestTransform;
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					for(int2 step: {int2(-1,0), int2(1,0),int2(0,-1),int2(0,1)}) { // Evaluate single steps along each translation axis
						int2 offset = int(exp2(levelCount-1-level)) * step;
						Transform transform = levelBestTransform * Transform{vec2(offset)/vec2(B.size)};
						double energy = SSE(A, B, transform);
						if(energy < bestEnergy) {
							bestEnergy = energy;
							stepBestTransform = transform;
						}
					}
					if(stepBestTransform==levelBestTransform) break;
					levelBestTransform = stepBestTransform;
					//break;
				}
				if(levelBestTransform != Transform{vec2(0,0)}) {
					// Asserts higher level walk was not as large as too miss a better shorter alignment at lower levels
					double originalEnergy = SSE(A, B, Transform{vec2(0,0)});
					assert_(bestEnergy <= originalEnergy, bestEnergy, originalEnergy, bestTransform);
				}
				bestTransform = levelBestTransform;
			}
			transforms.append(bestTransform);
		}
#endif
		log(transforms);
		return transforms;
	}
};
