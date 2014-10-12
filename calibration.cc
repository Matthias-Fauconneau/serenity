#include "calibration.h"

static void calibrate(ImageF& target, const ImageSource& source, int2 size) {
    assert_(target);


    const float scale = float(size.x)/1000;
    assert_(scale == float(size.y)/750, size);
    // Hardcoded parameters
    const float textureFrequency = 1*scale; // Paper texture frequency
    const float lightingFrequency = 32*scale; // Lighting conditions frequency (64?)

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(source.count())) {
        for(uint component : range(3)) {
            SourceImage sourceImage = source.image(index, component, size);
            assert_(sourceImage.size == size);
            parallel_apply(target, [](float sum, float source) { return sum + source; }, target, sourceImage);
        }
    }

    // Normalizes sum by mean (DC)
    float factor = 1/mean(target);
    parallel_apply(target, [&](float v) {  return min(1.f, factor*v); }, target);

    // Low pass to filter texture and noise and high pass to filter lighting conditions
    ImageF image = bandPass(target, textureFrequency, lightingFrequency);

    // Adds DC back and clips values over 1
    parallel_apply(target, [&](float v) {  return min(1.f, 1+v); }, image);
}

void blurNormalize(ImageF& target, const ImageF& source) {
    // Parameter scaling
    int2 size = source.size;
    const float scale = float(size.x)/1000;
    assert_(scale == float(size.y)/750, size);
    // Hardcoded parameters
    const float spotLowThreshold = 32*scale; // Low threshold of spot frequency
    assert_(spotLowThreshold > 0);

    // Image processing
    target = gaussianBlur(move(target), source, spotLowThreshold);
    target -= parallel_minimum(target);
    float max = parallel_maximum(target);
    assert_(max);
    float factor = 1./max; // FIXME: single pass minmax
    target *= factor;
}


int64 Calibration::time() const {
    int64 sourceTime = max(::apply(source.count(), [&](size_t index) { return source.time(index); }));
    int64 version = parseDate(__DATE__ " " __TIME__)*1000000000l;
    return max(sourceTime, version);
}

SourceImage Calibration::attenuation(int2 size) const {
    return cache<ImageF>("attenuation", "Calibration", source.folder, [&](TargetImage& target) {
            target.resize(size);
            calibrate(target, source, size);
    }, time(), size);
}

SourceImage Calibration::blendFactor(int2 size) const {
    return cache<ImageF>("blendFactor", "Calibration", source.folder, [&](TargetImage& target) {
        SourceImage source = attenuation(size);
        target.resize(source.size);
        blurNormalize(target, source.size);
    }, time(), size);
}
