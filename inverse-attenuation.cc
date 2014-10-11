#include "inverse-attenuation.h"
#include "parallel.h"
#include "math.h"

static float mix(float a, float b, float t) { return (1-t)*a + t*b; }

InverseAttenuation::InverseAttenuation(ImageFolder&& calibration) : Calibration(move(calibration), name()) {}
int64 InverseAttenuation::time() const { return max(ImageOperationT::time(), Calibration::time()); }
buffer<ImageF> InverseAttenuation::apply(const ImageF& red, const ImageF& green, const ImageF& blue) const {
    float blurRadius, correctionFactor;
    {ImageF source (red.size);
        parallel_apply(source, [&](float red, float green, float blue) { return red + green + blue; }, red, green, blue);

        // Splits image at spot frequency
        ImageF low = gaussianBlur(source, 32);
        ImageF high = source - low;

        // Detects low frequency background under spot
        float DC = mean(source);
        float lowEnergy = 0, highEnergy = 0;
        ref<float> weights = attenuation;
        chunk_parallel(source.buffer::size, [&](uint, size_t index) {
            float weight = 1-weights[index];
            lowEnergy += weight * sq(low[index]-DC);
            highEnergy += weight * sq(high[index]);
        });
        float ratio = lowEnergy / highEnergy;
        correctionFactor = clip(0.f, ratio/4, 1.f) + clip(0.f, ratio/8, 1.f);
        blurRadius = clip(8.f, 4*ratio, 32.f);
    }
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [&](const ImageF& source) {
        // Splits image at spot frequency
        ImageF low_spot = gaussianBlur(source, 8);
        ImageF high = source - low_spot;
        ImageF low = gaussianBlur(low_spot, 32);
        ImageF spot = low_spot - low;

        // Inverses attenuation using attenuation factors calibrated for each pixel
        ImageF target = low_spot / attenuation;

        // Blurs correction to attenuate miscalibration
        target = gaussianBlur(target, blurRadius); // Low pass correction to better correct uniform gradient
        // High pass to match source spot band
        ImageF low_corrected = gaussianBlur(target, 32);
        target -= low_corrected;

        // Merges correction near spot
        parallel_apply(target, [&](float low, float target, float spot, float factor, float high) {
            float mixed =
                    correctionFactor < 1 ? mix(spot, min(0.f, target), correctionFactor)
                                         : mix(min(0.f, target), target, correctionFactor-1);
            return low + (1-factor)*mixed + factor * spot + high;
        }, low, target, spot, blendFactor, high);

        // Never darkens
        parallel_apply(target, [&](float target, float source) { return max(target, source); }, target, source);
        return target;
    });
}
