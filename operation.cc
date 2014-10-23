#include "operation.h"

static void average(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) {
	parallel::apply(Y, [&](float r, float g, float b) {  return (r+g+b)/3; }, R, G, B);
}
void Intensity::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const {
	::average(Y, X0, X1, X2);
}

void Normalize::apply(const ImageF& Y, const ImageF& X) const {
	real mean = parallel::mean(X);
	real energy = parallel::energy(X, mean);
	real deviation = sqrt(energy / X.ref::size);
	parallel::apply(Y, [deviation, mean](const float value) { return (value-mean)/deviation; }, X);
}

void Multiply::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::mul(Y, X0, X1); }
