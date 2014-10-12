#include "calibration.h"
#include "math.h"

static void calibrate(ImageF& target, const ImageSource& source, int2 size) {
    assert_(target);

    // Hardcoded parameters
    const float scale = float(size.x)/1000;
    const float textureFrequency = 1*scale; // Paper texture frequency
    const float lightingFrequency = 64*scale; // Lighting conditions frequency

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(source.count())) {
        Time time; log_(str("Calibration.attenuation.source["+str(index)+']',size,""));
        SourceImageRGB sourceImage = source.image(index, size);
        log(time);
        assert_(sourceImage.size == size, sourceImage.size, size);
        parallel_apply(target, [](float sum, byte4 source) { return sum + float(int(source.b)+int(source.g)+int(source.r)); }, target, sourceImage);
        debug(break;)
    }

    // Normalizes sum by mean (DC)
    float factor = 1/mean(target);
    parallel_apply(target, [=](float v) { return min(1.f, factor*v); }, target);

    // Low pass to filter texture and noise and high pass to filter lighting conditions
    ImageF image = bandPass(target, textureFrequency, lightingFrequency);

    // Adds DC back and clips values over 1
    parallel_apply(target, [](float v) {  return min(1.f, 1+v); }, image);
}

void blurNormalize(ImageF& target, const ImageF& source) {
    // Hardcoded parameters
    int2 size = source.size;
    const float scale = float(size.x)/1000;
    const float spotLowThreshold = 32*scale; // Low threshold of spot frequency
    assert_(spotLowThreshold > 0);

    // Image processing
    gaussianBlur(target, source, spotLowThreshold);
    float min=inf, max=-inf;
    parallel_minmax(target, min, max);
    assert_(min < max, min, max);
    log(min, max);
    {const float scale = 1/(max-min);
        parallel_apply(target, [=](float value) { return scale*(value-min); }, target);}
}


int64 Calibration::time() const {
    int64 sourceTime = max(::apply(source.count(), [&](size_t index) { return source.time(index); }));
    int64 version = parseDate(__DATE__ " " __TIME__)*1000000000l;
    return max(sourceTime, version);
}

SourceImage Calibration::attenuation(int2 size) const {
    return cache<ImageF>("attenuation", "Calibration", source.folder, [&](TargetImage& target) {
            target.resize(size);
            Time time; log_(str("Calibration.attenuation",size,""));
            calibrate(target, source, size);
            log(time);
    }, time(), size);
}

SourceImage Calibration::blendFactor(const ImageF& attenuation) const {
    return cache<ImageF>("blendFactor", "Calibration", this->source.folder, [&](TargetImage& target) {
        target.resize(attenuation.size);
        Time time; log_(str("Calibration.blendFactor",attenuation.size,""));
        blurNormalize(target, attenuation);
        log(time);
    }, time(), attenuation.size);
}
