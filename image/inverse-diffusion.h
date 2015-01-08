#pragma once
#include "operation.h"
#include "calibration.h"

/// Inverts attenuation bias
struct InverseAttenuation : Calibration, ImageOperator, OperatorT<InverseAttenuation> {
	InverseAttenuation(ImageRGBSource& calibration) : Calibration(calibration) {}
	int64 time() const override { return max(OperatorT::time(), Calibration::time()); }
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
