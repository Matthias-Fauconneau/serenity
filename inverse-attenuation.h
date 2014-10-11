#pragma once
#include "parallel.h"
#include "image-operation.h"
#include "image-folder.h"

// Spot frequency bounds
const float lowBound = 8, highBound = 32; //TODO: automatic determination

void calibrate(const ImageF& target, ImageFolder& calibration, uint component) {
    assert_(target);

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(calibration.size())) {
        SourceImage source = calibration.image(index, component);
        // Low pass to filter texture, noise and lighting conditions
        //bandPass(target, lowBound, highBound);
        parallel_apply(target, [](float sum, float source) { return sum + source; }, target, source);
    }

#if 1
    // Normalizes sum by maximum
    float max = parallel_maximum(target);
    float factor = 1/max;
    parallel_apply(target, [&](float v) {  return factor*v; }, target);
#else
    // Normalizes sum by mean (DC) (and clips values over average to 1)
    float sum = parallel_sum(target);
    float mean = sum/target.buffer::size;
    float factor = 1/mean;
    parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);
#endif
}

/// Inverts attenuation bias
struct InverseAttenuation : ImageOperationT<InverseAttenuation> {
    SourceImage attenuation[3];

    /// Calibrates attenuation bias image by summing images of a white subject
    InverseAttenuation(ImageFolder&& calibration) {
        int64 calibrationTime = max(::apply(calibration.size(), [&](size_t index) { return calibration.time(index); }));
        for(uint component : range(3)) {
            attenuation[component] = cache<ImageF>("attenuation", name()+'.'+str(component), calibration.folder, [&](TargetImage& target) {
                    target.resize(calibration.imageSize);
                    calibrate(target, calibration, component);
            }, calibrationTime);
        }
    }

    void apply(const ImageF& target, const ImageF& source, uint component) const override {
        // Inverses attenuation using attenuation factors calibrated for each pixel
        parallel_apply(target, [&](float source, float bias) { return source / bias; }, source, attenuation[component]);

#if 1
        // Restricts correction to a frequency band
        ImageF reference = bandPass(source, lowBound, highBound);
        ImageF corrected = bandPass(target, lowBound, highBound);

        // Saturates correction below max(0, source) (prevents introduction of a light feature at spot frequency)
        parallel_apply(target, [&](float source, float reference, float corrected) {
            float saturated_corrected = min(corrected, max(reference, 0.f));
            float correction = saturated_corrected - reference;
            return source + correction;
        }, source, reference, corrected);
#endif

        // Never darkens
        parallel_apply(target, [&](float target, float source) { return max(target, source); }, target, source);
    }
};
