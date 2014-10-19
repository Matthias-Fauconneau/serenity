#include "normalize.h"
using namespace parallel;

/// Sums \a R+\a G+\a B into \a Y
static void sum(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) { apply(Y, [&](float r, float g, float b) {  return r+g+b; }, R, G, B); }

/// Normalizes mean and deviation
void Normalize::apply1(const ImageF& target, const ImageF& red, const ImageF& green, const ImageF& blue) const {
	::sum(target, red, green, blue);
	const float largeScale = (target.size.y-1)/6;
	gaussianBlur(target, target, largeScale/2); // Low pass [ .. largeScale/2]
	sub(target, target, gaussianBlur(target, largeScale)); // High pass [largeScale .. ]

	float energy = parallel::energy(target);
	float deviation = sqrt(energy / target.buffer::size);
	parallel::apply(target, [deviation](const float value) { return (1+(value)/deviation /*-1,1*/)/2 /*0,1*/; }, target);
}
