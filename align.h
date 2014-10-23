#pragma once
#include "transform.h"

/// Evaluates residual energy between A and transform B
static double SSE(const ImageF& A, const ImageF& B, Transform transform) {
	int2 offset = int2(round(transform.offset*vec2(B.size)));
	return SSE(A, B, offset);
}

#define MIPMAP 1
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
		array<Transform> transforms;
		transforms.append();
#if MULTISCALE || 1
		const int levelCount = log2(uint(images[0].size.x/8)); // / (32, 24)
#if MIPMAP
		array<ImageF> A = mipmap(images[0], levelCount);
		for(const ImageF& image : images.slice(1)) { // Compares each image with first one (TODO: full regression)
			array<ImageF> B = mipmap(image, levelCount);
			Transform bestTransform {vec2(0,0)};
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				const ImageF& a = A[levelCount-1-level];
				const ImageF& b = B[levelCount-1-level];
#else
		const ImageF& a = images[0];
		for(const ImageF& b : images.slice(1)) {
			Transform bestTransform {vec2(0,0)};
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
#endif
				Transform levelBestTransform = bestTransform;
				double bestEnergy = SSE(a, b, levelBestTransform);
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					for(int2 step: {int2(-1,0), int2(1,0),int2(0,-1),int2(0,1)}) { // Evaluate single steps along each translation axis
#if MIPMAP
						int2 offset = step;
#else
						int2 offset = int(exp2(levelCount-1-level)) * step;
#endif
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
#else // Exhaustive (TODO: coordinate walk)
		const ImageF& a = images[0];
		for(const ImageF& b : images.slice(1)) {
			float bestSSE = inf; int2 bestOffset = 0;
			const int2 delta = b.size / 7;
			const int steps = 5;
			for(int y: range(-steps, steps +1)) {
				for(int x: range(-steps, steps +1)) {
					int2 offset = (int2(x, y) * delta) / steps;
					float SSE = ::SSE(a, b, offset);
					if(SSE < bestSSE) { bestSSE = SSE, bestOffset = offset; }
				}
			}
			Transform bestTransform = Transform{vec2(bestOffset)/vec2(b.size)};
			log(delta, bestOffset);
#endif
			transforms.append(bestTransform);
		}
		log(transforms);
		return transforms;
	}
};
