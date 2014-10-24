#pragma once
#include "transform.h"

static double similarity(const ImageF& A, const ImageF& B, Transform transform) {
	assert_(transform.size == A.size && transform.size == B.size);
	return -SSE(A, B, transform.offset);
}

/// Evaluates first \a levelCount mipmap levels (shares source as first element)
static array<ImageF> mipmap(const ImageF& source, int levelCount) {
	array<ImageF> mipmap; mipmap.append( share(source) );
	for(int unused level: range(levelCount)) mipmap.append(downsample(mipmap.last()));
	return mipmap;
}

/// Aligns images
struct Align : ImageTransformGroupOperator, OperatorT<Align> {
	//string name() const override { return "Align"; }

	// Evaluates residual energy at integer offsets
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		for(auto& image: images) assert_(image.size == images[0].size);
		const int levelCount = log2(uint(images[0].size.x/8));
		array<ImageF> A = mipmap(images[0], levelCount);
		array<Transform> transforms;
		transforms.append(A[0].size, 0);
		for(const ImageF& image : images.slice(1)) { // Compares each image with first one
			array<ImageF> B = mipmap(image, levelCount);
			Transform bestTransform (B.last().size, 0);
			for(int level: range(levelCount)) { // From coarsest (last) to finest (first)
				const ImageF& a = A[levelCount-1-level];
				const ImageF& b = B[levelCount-1-level];
				Transform levelBestTransform (b.size, bestTransform.offset*b.size/bestTransform.size);
				real bestSimilarity = similarity(a, b, levelBestTransform);
				map<Transform, real> similarities;
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					for(int2 offset: {int2(0,-1), int2(-1,0),int2(1,0), int2(0,1)}) { // Evaluate single steps along each translation axis
						Transform transform = levelBestTransform * Transform(b.size, offset); real& similarity = similarities[transform];
						if(!similarity) similarity = ::similarity(a, b, transform);
						if(similarity > bestSimilarity) {
							bestSimilarity = similarity;
							stepBestTransform = transform;
						}
					}
					if(stepBestTransform==levelBestTransform) break;
					levelBestTransform = stepBestTransform;
				}
				bestTransform = levelBestTransform;
			}
			transforms.append(bestTransform);
		}
		return transforms;
	}
};
