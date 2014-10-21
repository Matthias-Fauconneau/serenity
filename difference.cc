#include "difference.h"

void Subtract::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::sub(Y, X0, X1); }
