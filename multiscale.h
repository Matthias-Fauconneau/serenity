#pragma once
#include "operator.h"

constexpr size_t bandCount = 8;

struct LowPass : ImageOperator1, OperatorT<LowPass> {
	string name() const override { return "[low]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};

/// Splits in bands
struct WeightFilterBank : ImageOperator, OperatorT<WeightFilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	string name() const override { return "[weightfilterbank]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Splits in bands
struct FilterBank : ImageOperator, OperatorT<FilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	string name() const override { return "[filterbank]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
