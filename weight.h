#pragma once
#include "operator.h"

struct Exposure : ImageOperator, OperatorT<Exposure> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
/*struct Saturation : ImageOperator, OperatorT<Saturation> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
struct Contrast : ImageOperator, OperatorT<Contrast> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Estimates exposure, saturation and contrast at every pixel
struct Weight : ImageOperator, OperatorT<Weight> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};*/

struct SelectMaximum : ImageGroupOperator, OperatorT<SelectMaximum> {
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Normalizes weights
/// \note if all weights are zero, weights are all set to 1/groupSize.
struct NormalizeSum : ImageGroupOperator, OperatorT<NormalizeSum> {
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

struct SmoothStep : ImageOperator1, OperatorT<SmoothStep> {
	void apply(const ImageF& Y, const ImageF& X) const override;
};
