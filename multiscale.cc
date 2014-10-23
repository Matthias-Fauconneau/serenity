#include "multiscale.h"

void WeightFilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& x = X[0];
	const float largeScale = (min(x.size.x,x.size.y)-1)/6;
	float octaveScale = largeScale;
	for(size_t index: range(Y.size)) {
		gaussianBlur(Y[index], X[0], octaveScale); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
	// Higher octaves are discarded
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

#if 0
void MultiBandWeight::apply(ref<ImageF> Y, ref<ImageF> X /*weight bands, source*/) const {
	const int bandCount = X.size-1;
	assert_(X.size > 2 && Y.size == 1, X.size, Y.size);
	ImageF r = copy(X.last()); // Remaining source octaves
	const float largeScale = (min(r.size.x,r.size.y)-1)/6;
	float octaveScale = largeScale;
	for(size_t index: range(bandCount-1)) {
		ImageF octave = gaussianBlur(r, octaveScale); // Splits lowest source octave
		if(index==0) parallel::mul(Y[0], X[index], octave); // Adds octave contribution
		else parallel::muladd(Y[0], X[index], octave); // Adds octave contribution
		assert_(Y[0].size == r.size, Y[0].size, r.size);
		parallel::sub(r, r, Y[0]); // Removes from remainder
		octaveScale /= 2; // Next octave
	}
	parallel::muladd(Y[0], X[bandCount-1], r); // Applies band weights remainder to source remainder
}
#endif
