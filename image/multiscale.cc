#include "multiscale.h"

void WeightFilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& x = X[0];
    float octaveScale = min(x.size.x, x.size.y)/2;
	for(size_t index: range(Y.size)) {
        gaussianBlur(Y[index], X[0], octaveScale, min(min(Y[index].size.x, Y[index].size.y)/2, (int)ceil(3*octaveScale))); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
}

inline void sub(mref<float> Y, ref<float> A, ref<float> B) { Y.apply([](float a, float b) { return a-b; }, A, B); }

void FilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& r = Y[Y.size-1];
	r.copy(X[0]); // Remaining octaves
    float octaveScale = min(r.size.x, r.size.y)/2;
	for(size_t index: range(Y.size-1)) {
        gaussianBlur(Y[index], r, octaveScale, min(min(Y[index].size.x, Y[index].size.y)/2, (int)ceil(3*octaveScale))); // Splits lowest octave
        sub(r, r, Y[index]); // Removes from remainder
		octaveScale /= 2; // Next octave
	}
	// Y[bandCount-1] holds remaining high octaves
}
