#include "image-operation.h"

/// Normalizes mean and deviation
struct Normalize : ImageOperationT<Normalize> {
	int outputs() const override { return 1; }
	void apply1(const ImageF& Y, const ImageF& red, const ImageF& green, const ImageF& blue) const override;
};
