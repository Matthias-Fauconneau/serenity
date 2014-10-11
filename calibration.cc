#include "calibration.h"

void calibrate(ImageF& target, ImageFolder& calibration, uint component) {
    assert_(target);

    // Sums all images
    target.buffer::clear();
    float DC = 0;
    for(size_t index: range(calibration.size())) {
        SourceImage source = calibration.image(index, component);
        // Low pass to filter texture, noise and lighting conditions
        DC += mean(source);
        ImageF image = bandPass(source, 1, 128);
        parallel_apply(target, [](float sum, float source) { return sum + source; }, target, image);
    }

    //target = gaussianBlur(move(target), 32);
    float factor = 1/DC;
    parallel_apply(target, [&](float v) {  return 1 + factor*v; assert_(1 + factor*v > 0); }, target);
#if 0
    /*if(1) {
        // Normalizes sum by maximum
        float max = parallel_maximum(target);
        float factor = 1/max;
        parallel_apply(target, [&](float v) {  return factor*v; }, target);
    } *//*else*/ if(1) {
        // Normalizes sum by mean (DC) (and clips values over average to 1)
        float factor = 1/mean(target);
        parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);
    } /*else {
        // Normalizes sum by mean (DC) (and clips values over average to 1)
        float factor = 1/mean(target);
        parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);
        // Offsets to 0
        target -= parallel_minimum(target);
    }*/
#endif
}

/// Calibrates attenuation bias image by summing images of a white subject
Calibration::Calibration(ImageFolder&& calibration, string name) {
    int64 calibrationTime = max(::apply(calibration.size(), [&](size_t index) { return calibration.time(index); }));
    for(uint component : range(3)) {
        attenuation[component] = cache<ImageF>("attenuation", name+'.'+str(component), calibration.folder, [&](TargetImage& target) {
                target.resize(calibration.imageSize);
                calibrate(target, calibration, component);
        }, calibrationTime);
    }
}

int64 Calibration::time() const { return parseDate(__DATE__ " " __TIME__)*1000000000l; }

/// Returns calibration image as sRGB visualization
Image Calibration::calibrationImage() const { return sRGB(attenuation[0], attenuation[1], attenuation[2]); }
