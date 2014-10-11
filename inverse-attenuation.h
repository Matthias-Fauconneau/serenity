#pragma once
#include "parallel.h"
#include "image-operation.h"
#include "image-folder.h"
#include "math.h"
#include "calibration.h"

/// Inverts attenuation bias
struct InverseAttenuation : Calibration, ImageOperationT<InverseAttenuation> {
    InverseAttenuation(ImageFolder&& calibration) : Calibration(move(calibration), name()) {}

    int64 time() const override { return max(ImageOperationT::time(), Calibration::time()); }

    void apply(ImageF& target, const ImageF& source) const override {
        // Inverses attenuation using attenuation factors calibrated for each pixel
        parallel_apply(target, [&](float source, float bias) { return source / bias; }, source, attenuation);

        target = gaussianBlur(move(target), target, 32);

        // Merges blurred corrected image near spot
        parallel_apply(target, [&](float low, float source, float factor) {
            return (1-factor) * low + factor * source;
        }, target, source, blendFactor);

        // Never darkens
        parallel_apply(target, [&](float target, float source) { return max(target, source); }, target, source);
    }
};
