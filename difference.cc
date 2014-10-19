#include "difference.h"

void Subtract::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 2);
	parallel::sub(Y[0], X[0], X[1]);
}
