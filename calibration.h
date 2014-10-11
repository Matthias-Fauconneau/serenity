#pragma once
#include "image-folder.h"

struct Calibration {
    SourceImage attenuation;
    SourceImage blendFactor;

    /// Calibrates attenuation bias image by summing images of a white subject
    Calibration(ImageFolder&& calibration, string name);

    int64 time() const;

    /// Returns calibration image as sRGB visualization
    Image attenuationImage() const;
    /// Returns blend factor image as sRGB visualization
    Image blendFactorImage() const;
};
