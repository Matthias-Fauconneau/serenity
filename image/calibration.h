#pragma once
#include "image-folder.h"

/// Calibrates attenuation bias image by summing images of a white subject
struct Calibration {
	ImageRGBSource& source;
	String path() const /*override*/ { return source.path()+"/Calibration"_; };

    /// Calibrates attenuation bias image by summing images of a white subject
	Calibration(ImageRGBSource& source) : source(source) {}

    int64 time() const;

    /// Returns calibration sum image
    SourceImage sum(int2 hint) const;
    /// Returns spot position (argmin sum)
    int2 spotPosition(int2 size) const;
    /// Returns spot size
    int2 spotSize(int2 size) const;
    /// Returns attenuation image (clip scale crop sum)
    SourceImage attenuation(int2 size) const;
};
