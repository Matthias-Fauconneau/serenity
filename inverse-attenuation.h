#pragma once
#include "parallel.h"
#include "image-operation.h"
#include "image-folder.h"

void calibrate(const ImageF& target, ImageFolder& calibration, uint component) {
    assert_(target);

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(calibration.size())) {
        SourceImage source = calibration.image(index, component);
        parallel_apply(target, [](float sum, float source) { return sum + source; }, target, source);
    }

    // Low pass to filter texture and noise
    gaussianBlur(target, 8); // Useful?, TODO?: weaken near spot, strengthen outside

    // TODO: High pass to filter lighting conditions

    // Normalizes sum by mean (DC) (and clips values over average to 1)
    float sum = parallel_sum(target);
    float mean = sum/target.buffer::size;
    float factor = 1/mean;
    parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);
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

        // Restricts correction to a frequency band
        const float lowPass = 8, highPass = 32; //TODO: automatic determination from spectrum of correction (difference) image
        ImageF reference = bandPass(source, lowPass, highPass);
        ImageF corrected = bandPass(target, lowPass, highPass);

        // Saturates correction below max(0, source) (prevents introduction of a light feature at spot frequency)
        parallel_apply(target, [&](float source, float reference, float corrected) {
            float saturated_corrected = min(corrected, max(reference, 0.f));
            float correction = saturated_corrected - reference;
            return source + correction;
        }, source, reference, corrected);
    }
};
