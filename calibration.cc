#include "calibration.h"
#include "math.h"

static void calibrate(const ImageF& target, const ImageSource& source) {
    // Hardcoded parameters
    int2 size = target.size;
    const float scale = float(size.x)/1000;
    const float textureFrequency = 1*scale; // Paper texture frequency
    const float lightingFrequency = 64*scale; // Lighting conditions frequency

    // Sums all images
    target.buffer::clear();
    for(size_t index: range(source.count())) {
        SourceImageRGB sourceImage = source.image(index, size);
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

static void blurNormalize(const ImageF& target, const ImageF& source) {
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
        parallel_apply(target, [=](float value) { return 1-scale*(value-min); }, target);}
}


int64 Calibration::time() const {
    int64 sourceTime = max(::apply(source.count(), [&](size_t index) { return source.time(index); }));
    int64 version = parseDate(__DATE__ " " __TIME__)*1000000000l;
    return max(sourceTime, version);
}

SourceImage Calibration::attenuation(int2 size) const {
    return cache<ImageF>(source.folder, "Calibration", "attenuation", size, time(), [&](TargetImage& target) {
            target.resize(size);
            Time time; log_(str("Calibration.attenuation",size,""));
            calibrate(target, source);
            log(time);
    });
}

SourceImage Calibration::blendFactor(int2 size) const {
    return cache<ImageF>(source.folder, "Calibration", "blendFactor", size, time(), [&](TargetImage& target) {
        target.resize(size);
        Time time; log_(str("Calibration.blendFactor",size,""));
        blurNormalize(target, attenuation(size));
        log(time);
    });
}

//FIXME: cache
Region Calibration::regionOfInterest(int2 size) const {
    return cache<Region>(source.folder, "Calibration", "blendFactor", strx(size), time(), [&]() {
        SourceImage source = blendFactor(size);
        int2 minimums[threadCount], maximums[threadCount];
        parallel_chunk(size.y, [&](uint id, uint64 start, uint64 chunkSize) {
            int2 min = size, max = 0;
            for(size_t y: range(start, start+chunkSize)) {
                for(size_t x: range(size.x)) {
                    if(source(x,y)) {
                        min = ::min(min, int2(x,y));
                        max = ::max(max, int2(x,y));
                    }
                }
            }
            minimums[id] = min, maximums[id] = max;
        });
        return Region{min(minimums), max(maximums)};
    });
}
