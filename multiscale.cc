#include "multiscale.h"

void LowPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	gaussianBlur(Y, X, largeScale/4);
}

void HighPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	parallel::sub(Y, X, gaussianBlur(X, largeScale));
}

void WeightFilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& x = X[0];
	const float largeScale = (min(x.size.x,x.size.y)-1)/6;
	float octaveScale = largeScale;
	for(size_t index: range(Y.size /*-1*/)) {
		gaussianBlur(Y[index], X[0], octaveScale); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
	//Y[Y.size-1].copy(X[0]);
}

void WeightFilterBank2::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == bandCount);
	const ImageF& x = X[0];
	const float largeScale = (min(x.size.x,x.size.y)-1)/6;
	float octaveScale = largeScale;
	for(size_t index: range(Y.size /*-1*/)) {
		gaussianBlur(Y[index], X[index], octaveScale); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
	//Y[Y.size-1].copy(X[0]);
}

void FilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& r = Y[Y.size-1];
	r.copy(X[0]); // Remaining octaves
	const float largeScale = (min(r.size.x,r.size.y)-1)/6;
	float octaveScale = largeScale;
	for(size_t index: range(Y.size-1)) {
		gaussianBlur(Y[index], r, octaveScale); // Splits lowest octave
		parallel::sub(r, r, Y[index]); // Removes from remainder
		octaveScale /= 2; // Next octave
	}
	// Y[bandCount-1] holds remaining high octaves
}
