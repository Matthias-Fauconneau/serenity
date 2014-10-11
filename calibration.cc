#include "calibration.h"

void calibrate(ImageF& target, ImageFolder& calibration) {
    assert_(target);

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(calibration.size())) {
        for(uint component : range(3)) {
            SourceImage source = calibration.image(index, component);
            parallel_apply(target, [](float sum, float source) { return sum + source; }, target, source);
        }
    }

    // Normalizes sum by mean (DC)
    float factor = 1/mean(target);
    parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);

    // Low pass to filter texture and noise and high pass to filter lighting conditions
    ImageF image = bandPass(target, 1, 64);

    // Adds DC back and clips values over 1
    parallel_apply(target, [&](float v) {  return min(1.f, 1+v); }, image);
}

void blurNormalize(ImageF& target, const ImageF& source) {
    target = gaussianBlur(move(target), source, 32);
    target -= parallel_minimum(target);
    float max = parallel_maximum(target);
    assert_(max);
    float factor = 1./max; // FIXME: single pass minmax
    target *= factor;
}

/// Calibrates attenuation bias image by summing images of a white subject
Calibration::Calibration(ImageFolder&& calibration, string name) {
    int64 calibrationTime = max(::apply(calibration.size(), [&](size_t index) { return calibration.time(index); }));
    attenuation = cache<ImageF>("attenuation", name, calibration.folder, [&](TargetImage& target) {
            log("Calibration");
            target.resize(calibration.imageSize);
            calibrate(target, calibration);
    }, calibrationTime);
    blendFactor = cache<ImageF>("blendFactor", name, calibration.folder, [&](TargetImage& target) {
            target.resize(calibration.imageSize);
            blurNormalize(target, attenuation);
    }, calibrationTime);
}

int64 Calibration::time() const { return parseDate(__DATE__ " " __TIME__)*1000000000l; }

Image Calibration::attenuationImage() const { return sRGB(attenuation); }
Image Calibration::blendFactorImage() const { return sRGB(blendFactor); }
