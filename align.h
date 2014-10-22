#pragma once
#include "transform.h"

/// Evaluates residual energy between A and transform B
static double residualEnergy(const ImageF& A, const ImageF& B, Transform transform) {
	assert_(A.size == B.size);
	int2 margin = int2(round(abs(transform.offset)*vec2(B.size)));
	int2 size = A.size - 2*margin;
	assert_(size > int2(16), transform, A.size, margin, size);
	double energy = sumXY(size, [&A, &B, transform, margin](int x, int y) {
		int2 a = margin+int2(x,y);
		int2 b = int2(round(transform(a, B.size)));
		assert_(a >= int2(0) && a < A.size);
		assert_(b >= int2(0) && b < B.size);
		return sq(A(a) - B(b)); //SSE
	}, 0.0);
	energy /= size.x*size.y;
	return energy;
}

/// Evaluates first \a levelCount mipmap levels (shares source as first element)
static array<ImageF> mipmap(const ImageF& source, int levelCount) {
	array<ImageF> mipmap; mipmap.append( share(source) );
	for(int unused level: range(levelCount)) mipmap.append(downsample(mipmap.last()));
	return mipmap;
}

/// Aligns images
struct Align : ImageTransformGroupOperation, OperationT<Align> {
	string name() const override { return "[align]"; }

	// Evaluates residual energy at integer offsets
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		for(auto& image: images) assert_(image.size == images[0].size);
		const int levelCount = log2(uint(images[0].size.x/128)); // / (32, 24)
		array<ImageF> A = mipmap(images[0], levelCount);
		array<Transform> transforms;
		transforms.append();
		for(const ImageF& image : images.slice(1)) { // Compares each image with first one (TODO: full regression)
			array<ImageF> B = mipmap(image, levelCount);
			Transform bestTransform {vec2(0,0)};
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				const ImageF& a = A[levelCount-1-level];
				const ImageF& b = B[levelCount-1-level];
				Transform levelBestTransform = bestTransform;
				double bestResidualEnergy = residualEnergy(a, b, levelBestTransform);
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					for(int2 offset: {int2(-1,0), int2(1,0),int2(0,-1),int2(0,1)}) { // Evaluate single steps along each translation axis
						Transform transform = levelBestTransform * Transform{vec2(offset)/vec2(b.size)};
						double energy = residualEnergy(a, b, transform);
						if(energy < bestResidualEnergy) {
							bestResidualEnergy = energy;
							stepBestTransform = transform;
						}
					}
					if(stepBestTransform==levelBestTransform) break;
					levelBestTransform = stepBestTransform;
					//break;
				}
				if(levelBestTransform != Transform{vec2(0,0)}) {
					// Asserts higher level walk was not as large as too miss a better shorter alignment at lower levels
					double originalResidualEnergy = residualEnergy(a, b, Transform{vec2(0,0)});
					assert_(bestResidualEnergy <= originalResidualEnergy, bestResidualEnergy, originalResidualEnergy, bestTransform);
				}
				bestTransform = levelBestTransform;
			}
			transforms.append(bestTransform);
		}
		return transforms;
	}
};
