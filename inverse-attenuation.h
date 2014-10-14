#pragma once
#include "image-operation.h"
#include "image-folder.h"
#include "calibration.h"

/// Inverts attenuation bias
struct InverseAttenuation : Calibration, ImageOperationT<InverseAttenuation> {
    InverseAttenuation(const ImageSource& calibration) : Calibration(calibration) {}
    int64 time() const override { return max(ImageOperationT::time(), Calibration::time()); }
    buffer<ImageF> apply(const ImageF& red, const ImageF& green, const ImageF& blue) const override;
};
