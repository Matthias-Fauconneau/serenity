#pragma once
#include "operator.h"

constexpr size_t bandCount = 3;

struct LowPass : ImageOperator1, OperatorT<LowPass> { void apply(const ImageF& Y, const ImageF& X) const override; };
struct HighPass : ImageOperator1, OperatorT<HighPass> { void apply(const ImageF& Y, const ImageF& X) const override; };

/// Splits in bands
struct WeightFilterBank : ImageOperator, OperatorT<WeightFilterBank> {
	//string name() const override { return "WeightFilterBank"; }
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Splits in bands
struct WeightFilterBank2 : ImageOperator, OperatorT<WeightFilterBank2> {
	//string name() const override { return "WeightFilterBank"; }
	size_t inputs() const override { return bandCount; }
	size_t outputs() const override { return bandCount; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Splits in bands
struct FilterBank : ImageOperator, OperatorT<FilterBank> {
	//string name() const override { return "FilterBank"; }
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return bandCount; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
