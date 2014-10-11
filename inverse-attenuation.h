#pragma once
#include "image-operation.h"
#include "image-folder.h"
#include "calibration.h"

/// Inverts attenuation bias
struct InverseAttenuation : Calibration, ImageOperationT<InverseAttenuation> {
    InverseAttenuation(ImageFolder&& calibration);
    int64 time() const override;
    buffer<ImageF> apply(const ImageF& red, const ImageF& green, const ImageF& blue) const override;
};
