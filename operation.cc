#include "operation.h"

void Subtract::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::sub(Y, X0, X1); }

void Multiply::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::mul(Y, X0, X1); }

void Divide::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::div(Y, X0, X1); }

static void average(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) {
	parallel::apply(Y, [&](float r, float g, float b) {  return (r+g+b)/3; }, R, G, B);
}
void Intensity::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const {
	::average(Y, X0, X1, X2);
}

void LowPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	gaussianBlur(Y, X, largeScale/4); // Low pass
}

/*void HighPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	parallel::sub(Y, X, gaussianBlur(X, largeScale/4)); // High pass [ .. largeScale/4]
}

void BandPass::apply(const ImageF& Y, const ImageF& X) const {
	const float largeScale = (X.size.y-1)/6;
	gaussianBlur(Y, X, largeScale/8); // Low pass [largeScale/2 .. ]
	parallel::sub(Y, Y, gaussianBlur(Y, largeScale/2)); // High pass [ .. largeScale]
}*/

void Normalize::apply(const ImageF& Y, const ImageF& X) const {
	real mean = parallel::mean(X);
	real energy = parallel::energy(X, mean);
	real deviation = sqrt(energy / X.ref::size);
	parallel::apply(Y, [deviation, mean](const float value) { return (value-mean)/deviation; }, X);
}
