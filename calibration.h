#pragma once
#include "image-folder.h"

struct Region {
    int2 min, max;
};

/// Calibrates attenuation bias image by summing images of a white subject
struct Calibration {
    const ImageSource& source;

    /// Calibrates attenuation bias image by summing images of a white subject
    Calibration(const ImageSource& source) : source(source) {}

    int64 time() const;

    /// Returns calibration image
    SourceImage attenuation(int2 hint) const;
    /// Returns blend factor image
    SourceImage blendFactor(int2 size) const;
    /// Returns region of interest
    Region regionOfInterest(int2 size) const;

    /*/// Returns calibration image as sRGB visualization
    Image attenuationRGB() const;
    /// Returns blend factor image as sRGB visualization
    Image blendFactorRGB() const;*/
};
