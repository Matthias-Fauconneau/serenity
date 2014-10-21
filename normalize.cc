#include "normalize.h"
using namespace parallel;

/// Averages \a R+\a G+\a B into \a Y
static void average(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) {
	apply(Y, [&](float r, float g, float b) {  return (r+g+b)/3; }, R, G, B);
}

void Intensity::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const {
	::average(Y, X0, X1, X2);
	assert_(isNumber(Y[0]));
}

void LowPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	gaussianBlur(Y, X, largeScale/16); // Low pass
}

void BandPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	gaussianBlur(Y, X, largeScale/2); // Low pass [ .. largeScale/2]
	sub(Y, X, gaussianBlur(X, largeScale)); // High pass [largeScale .. ]
}

void Normalize::apply(const ImageF& Y, const ImageF& X) const {
	float energy = parallel::energy(X);
	float deviation = sqrt(energy / X.ref::size);
	parallel::apply(Y, [deviation](const float value) { return (1+(value)/deviation /*-1,1*/)/2 /*0,1*/; }, X);
}
