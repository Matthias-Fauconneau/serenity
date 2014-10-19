#include "normalize.h"
using namespace parallel;

/// Sums \a R+\a G+\a B into \a Y
static void sum(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) { apply(Y, [&](float r, float g, float b) {  return r+g+b; }, R, G, B); }

/// Normalizes mean and deviation
void Normalize::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const {
	::sum(Y, X0, X1, X2);
	const float largeScale = (Y.size.y-1)/6;
	gaussianBlur(Y, Y, largeScale/2); // Low pass [ .. largeScale/2]
	sub(Y, Y, gaussianBlur(Y, largeScale)); // High pass [largeScale .. ]

	float energy = parallel::energy(Y);
	float deviation = sqrt(energy / Y.ref::size);
	assert_(deviation, energy, Y);
	parallel::apply(Y, [deviation](const float value) { return (1+(value)/deviation /*-1,1*/)/2 /*0,1*/; }, Y);
}
