#include "operation.h"

/// Normalizes mean and deviation
struct Normalize : ImageOperation31, OperationT<Normalize> {
	string name() const override { return "[normalize]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const override;
};
