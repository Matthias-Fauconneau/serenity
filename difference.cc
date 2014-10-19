#include "difference.h"

void Difference::apply1(const ImageF& Y, const ImageF& A, const ImageF& B) const {
	parallel::sub(Y, A, B);
}
