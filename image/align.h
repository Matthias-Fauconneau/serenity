#pragma once
#include "transform.h"
inline uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }

static double similarity(const ImageF& A, const ImageF& B, Transform transform) {
	assert_(transform.size == A.size && transform.size == B.size);
	return -SSE(A, B, transform.offset);
}

/// Evaluates first \a levelCount mipmap levels (shares source as first element)
static array<ImageF> mipmap(const ImageF& source, int levelCount) {
    array<ImageF> mipmap; mipmap.append( unsafeRef(source) );
	for(int unused level: range(levelCount)) {
		mipmap.append(downsample(mipmap.last()));
		assert_(mipmap.last());
	}
	return mipmap;
}

/// Aligns images
struct Align : ImageTransformGroupOperator, OperatorT<Align> {
	// Evaluates residual energy at integer offsets
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		for(auto& image: images) assert_(image.size == images[0].size);
        const int levelCount = log2(uint(min(images[0].size.x, images[0].size.y)/8));
		array<ImageF> A = mipmap(images[0], levelCount);
		array<Transform> transforms;
		transforms.append(A[0].size, 0);
        log("Align"); Time time{true};
		for(const ImageF& image : images.slice(1)) { // Compares each image with next one
			array<ImageF> B = mipmap(image, levelCount);
            Transform bestTransform (B[0].size, int2(-B[0].size.x*1/2, 0)); // Initializes right of previous image
            for(int level: range(max(0, levelCount-8), levelCount)) { // From coarsest (last 2Kpx) to finest (first 8px)
				const ImageF& a = A[levelCount-1-level];
                const ImageF& b = B[levelCount-1-level];
                Transform levelBestTransform (b.size, bestTransform.offset*b.size/bestTransform.size);
                if(level >= levelCount-2) { bestTransform = levelBestTransform; continue; } // Skips highest resolutions
                double bestSimilarity = similarity(a, b, levelBestTransform);
                map<Transform, double> similarities;
				for(;;) { // Integer walk toward minimum at this scale (TODO: better multiscale to avoid long walks)
					Transform stepBestTransform = levelBestTransform;
					//for(int2 offset: {int2(0,-1), int2(-2, 0), int2(-1,0), int2(1,0), int2(2, 0), int2(0,1)}) { // Evaluate single steps along each translation axis
					for(int2 offset: {
																										  int2( 0, -2),
																					  int2(-1, -1), int2( 0, -1), int2(1, -1),
																	int2(-2, 0), int2(-1,  0),                     int2(1,  0), int2(2, 0),
																					  int2(-1,  1), int2( 0,  1), int2(1,  1),
																										  int2(0, 2) }) {
                        Transform transform = levelBestTransform * Transform(b.size, offset); double& similarity = similarities[transform];
                        //if(transform.offset.x > -B[0].size.x*3/4) continue; // Restricts overlap for similar images
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
			transforms.append( transforms.last() * bestTransform );
			A = move(B); // Compares each image with next one
		}
        log(time);
		return transforms;
	}
};
