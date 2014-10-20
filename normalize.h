#include "operation.h"

/// Sums 3 components
struct Intensity : ImageOperation31, OperationT<Intensity> {
	string name() const override { return "[intensity]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const override;
};

/// Selects scales between [(size.y-1)/6 .. (size.y-1)/12]
struct BandPass : ImageOperation1, OperationT<BandPass> {
	string name() const override { return "[bandpass]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};

/// Normalizes mean and deviation
struct Normalize : ImageOperation1, OperationT<Normalize> {
	string name() const override { return "[normalize]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};
