#pragma once
#include "operator.h"

/// Estimates exposure, saturation (TODO) and contrast at every pixel
struct Weight : ImageOperator, OperatorT<Weight> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

struct SelectMaximum : ImageGroupOperator, OperatorT<SelectMaximum> {
	//string name() const override { return "SelectMax"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Normalizes weights
/// \note if all weights are zero, weights are all set to 1/groupSize.
struct NormalizeSum : ImageGroupOperator, OperatorT<NormalizeSum> {
	//string name() const override { return "NormalizeSum"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
