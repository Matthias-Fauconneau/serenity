#pragma once
#include "image-folder.h"

struct Region {
    int2 min, max;
};
inline String str(Region o) { return str(o.min, o.max); }

/// Calibrates attenuation bias image by summing images of a white subject
struct Calibration {
    const ImageSource& source;

    /// Calibrates attenuation bias image by summing images of a white subject
    Calibration(const ImageSource& source) : source(source) {}

    int64 time() const;

    /// Returns calibration sum image
    SourceImage sum(int2 hint) const;
    /// Returns spot position (argmin sum)
    int2 spotPosition(int2 size) const;
    /// Returns spot size
    int2 spotSize(int2 size) const { return size/4; }
    /// Returns attenuation image (crop filter sum)
    SourceImage attenuation(int2 size) const;
    /// Returns blend factor image (lowpass attenuation)
    SourceImage blendFactor(int2 size) const;
};
