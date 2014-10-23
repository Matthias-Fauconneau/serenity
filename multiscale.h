#pragma once
#include "operator.h"

constexpr size_t bandCount = 2;

/// Splits in bands
struct WeightFilterBank : ImageOperator, OperatorT<WeightFilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	string name() const override { return "[filterbank]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Splits in bands
struct FilterBank : ImageOperator, OperatorT<FilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	string name() const override { return "[filterbank]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

#if 0
/// Applies weights bands to source bands
struct MultiBandWeight : ImageOperator, OperatorT<MultiBandWeight> {
	size_t inputs() const override { return 0; }
	size_t outputs() const override { return 1; }
	string name() const override { return "[multibandweight]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X /*weight bands, source*/) const override;
};
#endif
