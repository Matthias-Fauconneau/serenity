#pragma once
#include "operator.h"

/// Converts single component image group to a multiple component image
struct Prism : ImageGroupOperator, OperatorT<Prism> {
	//string name() const override { return "Prism"; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
