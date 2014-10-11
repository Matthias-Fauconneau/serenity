#pragma once
#include "parallel.h"
#include "image-operation.h"
#include "image-folder.h"
#include "math.h"
#include "calibration.h"

/// Inverts attenuation bias
struct InverseAttenuation : Calibration, ImageOperationT<InverseAttenuation> {
    InverseAttenuation(ImageFolder&& calibration) : Calibration(move(calibration), name()) {}

    int64 time() const override { return Calibration::time(); }

    void apply(ImageF& target, const ImageF& source, uint component) const override {
        // Inverses attenuation using attenuation factors calibrated for each pixel
        parallel_apply(target, [&](float source, float bias) { return bias ? source / bias : 0; }, source, attenuation[component]);

        if(1) {
#if 0
            ImageF reference, corrected;
            if(0) { // Filters DC
                reference = source-mean(source);
                corrected = target-mean(target);
            } else if(1) { // Selects high frequencies

                reference = highPass(source, 64);
                corrected = highPass(target, 64);
                //target.copy(reference); return;
            } else { // Selects frequencies within a band
                reference = bandPass(source, 1, 128/*highThreshold*/);
                corrected = bandPass(target, 1, 128/*highThreshold*/);
            }
            // Saturates correction below max(0, source) (May flatten but not lighten the signal above surroundings) (TODO: in each band)
            parallel_apply(target, [&](float source, float referenceLow, float referenceHigh, float corrected) {
                float saturated_corrected = min(corrected, max(referenceHigh, 0.f) + max(referenceLow, 0.f));
                float correction = saturated_corrected - reference;
                return source + correction;
            }, source, reference, corrected);
#elif 0
            // Splits source in bands (scales)
            ImageF low = lowPass(source, 16);
            ImageF high = source - low;

            // Saturates correction to flattening (TODO: at every scale)
            parallel_apply(target, [&](float target, float low, float high) {
                return min(target, max(low, 0.f) + max(high, 0.f));
            }, target, low, high);
#elif 0
            // Splits source in bands (scales) from high to low
            array<ImageF> bands;
            ImageF remainder = share(source);
            for(float split=4; split<=64; split+=4) {
                ImageF low = lowPass(remainder, split);
                bands.append( remainder - low ); // Scales between previous split and current split
                remainder = move(low);
            }
            bands.append(move(remainder)); // Lowest band (DC to last (coarsest) split)

            // Saturates brightness increase to flattening at every scale
            parallel_apply(target, [&](size_t index) {
                float flat = 0;
                for(const ImageF& image: bands) flat += max(0.f, image[index]);
                return flat; //min(target, flat);
            });

            // TODO: continuous version in spectral space ?
#elif 1
            target = gaussianBlur(move(target), target, 32 ); // 32 is enough
            // Low pass near spot

            parallel_apply(target, [&](float low, float source, float a) {
                return low; //(1-a) * low + a * source; //min(target, flat);
            }, target, source, attenuation[component]);

#else
            //target = gaussianBlur(move(target), source, 1); return;
            //abs(target, lowPass(source, 64)); return; // 4
#endif
        }

        // Never darkens
        parallel_apply(target, [&](float target, float source) { return max(target, source); }, target, source);
    }
};
