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
    SourceImage blendFactor = Calibration::blendFactor(size);
    /*Region regionOfInterest = Calibration::regionOfInterest(size);
    log(regionOfInterest);*/

    // Parameter scaling
    const float scale = float(size.x)/1000;
    // Hardcoded parameters
    const float spotHighThreshold = 8*scale; // High threshold of spot frequency
    const float spotLowThreshold = 32*scale; // Low threshold of spot frequency
    // Estimated parameters
    float blurRadius, correctionFactor;

    // Parameter estimation (FIXME: separate (cached) pass to switch back to component-wise)
    {ImageF source (size);
        parallel_apply(source, [](float red, float green, float blue) { return red + green + blue; }, red, green, blue);

        // Splits image at spot frequency
        ImageF low = gaussianBlur(source, spotLowThreshold);

        // Detects uniform background under spot
        ImageF lowlow = gaussianBlur(low, 64*scale);
        ref<float> weights = blendFactor;
        float lowEnergy = parallel_sum(lowlow, [=](float v, float w) { return w * sq(v); }, 0.f, weights);
        float highEnergy = parallel_sum(source, [=](float v, float bg, float w) { return w * sq(v-bg); }, 0.f, lowlow, weights);
        assert_(highEnergy > 0, lowEnergy, highEnergy);
        float ratio = lowEnergy / highEnergy;
        correctionFactor = clip(0.f, (ratio-1)/2, 1.f) + clip(0.f, (ratio-2)/8, 1.f);
        blurRadius = clip(spotHighThreshold, (ratio-12)*4*scale, spotLowThreshold);
        log(withName(lowEnergy, highEnergy, ratio, correctionFactor, blurRadius/scale));
    }

    // Image processing
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [&](const ImageF& source) {
        // Splits image at spot frequency
        ImageF low_spot = gaussianBlur(source, spotHighThreshold);
        ImageF high = source - low_spot;
        ImageF low = gaussianBlur(low_spot, spotLowThreshold);
        ImageF spot = low_spot - low;

        // Inverses attenuation using attenuation factors calibrated for each pixel
        ImageF target = move(low_spot) / attenuation;

        // Blurs correction to attenuate miscalibration
        gaussianBlur(target, target, blurRadius); // Low pass correction to better correct uniform gradient
        // High pass to match source spot band
        ImageF low_corrected = gaussianBlur(target, spotLowThreshold);
        target -= low_corrected;

        // Merges correction near spot
        parallel_apply(target, [=](float low, float target, float spot, float factor, float high) {
            float mixed =
                    correctionFactor < 1 ? mix(spot, min(0.f, target), correctionFactor)
                                         : mix(min(0.f, target), target, correctionFactor-1);
            return low + factor*mixed + (1-factor) * spot + high;
        }, low, target, spot, blendFactor, high);

        // Never darkens
        parallel_apply(target, [](float target, float source) { return max(target, source); }, target, source);
        return target;
    });
}
