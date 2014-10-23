#pragma once
#include "operator.h"

/// Estimates contrast at every pixel
struct Contrast : ImageOperator1, OperatorT<Contrast> {
	string name() const override { return "[contrast]"; }
	void apply(const ImageF& Y, const ImageF& X) const;
};

struct MaximumWeight : ImageGroupOperator, OperatorT<MaximumWeight> {
	string name() const override { return "[maximum-weight]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Normalizes weights
/// \note if all weights are zero, weights are all set to 1/groupSize.
struct NormalizeWeights : ImageGroupOperator, OperatorT<NormalizeWeights> {
	string name() const override { return "[normalize-weights]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
