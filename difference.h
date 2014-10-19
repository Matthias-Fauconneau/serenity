#pragma once
#include "image-operation.h"

struct Difference : ImageOperationT<Difference> {
	int inputs() const override { return 2; }
	int outputs() const override { return 1; }
	void apply1(const ImageF& Y, const ImageF& A, const ImageF& B) const override;
};
