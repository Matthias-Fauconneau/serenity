#pragma once
#include "image-folder.h"

/// Calibrates attenuation bias image by summing images of a white subject
struct Calibration {
    const ImageSource& source;

    /// Calibrates attenuation bias image by summing images of a white subject
    Calibration(const ImageSource& source) : source(source) {}

    int64 time() const;

    /// Returns calibration image
    SourceImage attenuation(int2 hint) const;
    /// Returns blend factor image
    SourceImage blendFactor(const ImageF& attenuation) const;

    /*/// Returns calibration image as sRGB visualization
    Image attenuationRGB() const;
    /// Returns blend factor image as sRGB visualization
    Image blendFactorRGB() const;*/
};
