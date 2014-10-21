#include "difference.h"

void Subtract::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::sub(Y, X0, X1); }

void Multiply::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::mul(Y, X0, X1); }

void Divide::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::div(Y, X0, X1); }
