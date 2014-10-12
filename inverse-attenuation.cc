#include "inverse-attenuation.h"
#include "parallel.h"
#include "math.h"

static float mix(float a, float b, float t) { return (1-t)*a + t*b; }

InverseAttenuation::InverseAttenuation(const ImageSource& calibration) : Calibration(calibration) {}
int64 InverseAttenuation::time() const { return max(ImageOperationT::time(), Calibration::time()); }
buffer<ImageF> InverseAttenuation::apply(const ImageF& red, const ImageF& green, const ImageF& blue) const {
    int2 size = red.size; assert_(green.size == size && blue.size == size);

    // Calibration parameter
    SourceImage attenuation = Calibration::attenuation(size);
    SourceImage blendFactor = Calibration::blendFactor(attenuation);

    // Parameter scaling
    const float scale = float(size.x)/1000;
    // Hardcoded parameters
    const float spotHighThreshold = 8*scale; // High threshold of spot frequency
    assert_(spotHighThreshold > 0);
    const float spotLowThreshold = 32*scale; // Low threshold of spot frequency
    // Estimated parameters
    float blurRadius, correctionFactor;

    // Parameter estimation
    {ImageF source (size);
        parallel_apply(source, [](float red, float green, float blue) { return red + green + blue; }, red, green, blue);

        // Splits image at spot frequency
        ImageF low = gaussianBlur(source, spotLowThreshold);
        ImageF high = source - low;

        // Detects low frequency background under spot
        float DC = mean(source);
        ref<float> weights = attenuation;
        float lowEnergy = parallel_reduce(low, [=](float a, float v, float w) { float weight = 1 - w; return a + weight * sq(v-DC); }, 0.f, weights);
        float highEnergy = parallel_reduce(high, [=](float a, float v, float w) { float weight = 1 - w; return a + weight * sq(v); }, 0.f, weights);
        assert_(lowEnergy > 0 && highEnergy > 0, lowEnergy, highEnergy);
        log(lowEnergy, highEnergy);
        float ratio = lowEnergy / highEnergy;
        correctionFactor = clip(0.f, ratio/4, 1.f) + clip(0.f, ratio/8, 1.f);
        blurRadius = clip(spotHighThreshold, 4*ratio, spotLowThreshold);
    }

    // Image processing
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [&](const ImageF& source) {
        // Splits image at spot frequency
        ImageF low_spot = gaussianBlur(source, spotHighThreshold);
        ImageF high = source - low_spot;
        ImageF low = gaussianBlur(low_spot, spotLowThreshold);
        ImageF spot = low_spot - low;

        // Inverses attenuation using attenuation factors calibrated for each pixel
        ImageF target = low_spot / attenuation;

        // Blurs correction to attenuate miscalibration
        target = gaussianBlur(target, blurRadius); // Low pass correction to better correct uniform gradient
        // High pass to match source spot band
        ImageF low_corrected = gaussianBlur(target, spotLowThreshold);
        target -= low_corrected;

        // Merges correction near spot
        parallel_apply(target, [=](float low, float target, float spot, float factor, float high) {
            float mixed =
                    correctionFactor < 1 ? mix(spot, min(0.f, target), correctionFactor)
                                         : mix(min(0.f, target), target, correctionFactor-1);
            return low + (1-factor)*mixed + factor * spot + high;
        }, low, target, spot, blendFactor, high);

        // Never darkens
        parallel_apply(target, [](float target, float source) { return max(target, source); }, target, source);
        return target;
    });
}