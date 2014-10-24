#include "multiscale.h"

void WeightFilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& x = X[0];
	const float largeScale = (min(x.size.x,x.size.y)-1)/6;
	float octaveScale = largeScale / 4;
	for(size_t index: range(Y.size)) {
		gaussianBlur(Y[index], X[0], octaveScale); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
}

void FilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& r = Y[Y.size-1];
	r.copy(X[0]); // Remaining octaves
	const float largeScale = (min(r.size.x,r.size.y)-1)/6;
	float octaveScale = largeScale / 4;
	for(size_t index: range(Y.size-1)) {
		gaussianBlur(Y[index], r, octaveScale); // Splits lowest octave
		parallel::sub(r, r, Y[index]); // Removes from remainder
		octaveScale /= 2; // Next octave
	}
	// Y[bandCount-1] holds remaining high octaves
}
