#include "inverse-attenuation.h"
#include "parallel.h"
#include "math.h"

//static float mix(float a, float b, float t) { return (1-t)*a + t*b; }

/// Copies rectangle \a size around \a origin from \a (red+green+blue) into \a target
static void crop(const ImageF& target, const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin) {
    parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
        assert_(origin >= int2(0) && origin+target.size <= red.size, withName(origin, target.size, red.size) );
        size_t offset = origin.y*red.stride + origin.x;
        for(size_t y: range(start, start+chunkSize)) {
            for(size_t x: range(target.size.x)) {
                size_t index = offset + y*red.stride + x;
                target[y*target.stride + x] = red[index] + green[index] + blue[index];
            }
        }
    });
}
inline ImageF crop(ImageF&& target, const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin) {
    crop(target, red, green, blue, origin); return move(target);
}
inline ImageF crop(const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin, int2 size) {
    return crop(size, red, green, blue, origin);
}

/// Copies \a source with \a crop inserted at \a origin into \a target
static void insert(const ImageF& target, const ImageF& source, const ImageF& crop, int2 origin) {
    target.copy(source); // Assumes crop is small before source
    assert_(origin >= int2(0) && origin+crop.size <= target.size, origin, crop.size, target.size);
    parallel_chunk(crop.size.y, [&](uint, uint64 start, uint64 chunkSize) {
        size_t offset = origin.y*target.stride + origin.x;
        for(size_t y: range(start, start+chunkSize)) {
            for(size_t x: range(crop.size.x)) {
                size_t index = offset + y*target.stride + x;
                target[index] = crop[y*crop.stride + x];
            }
        }
    });
}
inline ImageF insert(ImageF&& target, const ImageF& source, const ImageF& crop, int2 origin) {
    insert(target, source, crop, origin); return move(target);
}
inline ImageF insert(const ImageF& source, const ImageF& crop, int2 origin) { return insert(source.size, source, crop, origin); }

InverseAttenuation::InverseAttenuation(const ImageSource& calibration) : Calibration(calibration) {}
int64 InverseAttenuation::time() const { return max(ImageOperationT::time(), Calibration::time()); }
buffer<ImageF> InverseAttenuation::apply(const ImageF& red, const ImageF& green, const ImageF& blue) const {
    const int2 size = red.size; assert_(green.size == size && blue.size == size);

    // Calibrated parameter
    const SourceImage attenuation = Calibration::attenuation(size);
    const int2 spotSize = Calibration::spotSize(size);
    const int2 spotOrigin = Calibration::spotPosition(size)-spotSize/2;

    // Estimated parameters
    log(spotOrigin, spotSize, size);
    const ImageF value = crop(red, green, blue, spotOrigin, spotSize);
    const int2 offset = argmin(value)-spotSize/2;
    log(offset);
    const int2 origin = clip(int2(0), spotOrigin+offset, size-spotSize); // Compenstates dust movement

    /*// Splits image frequencies to detect uniform background under spot
        ImageF low = gaussianBlur(source, 1.f/8*spotSize.x);
        float lowEnergy = energy(low);
        float highEnergy = parallel_sum(source, [](float source, float low) { return sq(source-low); }, 0.f, low);
        assert_(lowEnergy > 0 && highEnergy > 0, lowEnergy, highEnergy);
        float ratio = lowEnergy / highEnergy;
        correctionFactor = clip(0.f, (ratio-1)/2, 1.f) + clip(0.f, (ratio-2)/8, 1.f);
        blurRadius = clip(1.f/16, ratio/8, 1.f/8)*spotSize.x;
        log(withName(lowEnergy, highEnergy, ratio, correctionFactor, blurRadius/spotSize.x));}*/

    // Image processing
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [=,&attenuation](const ImageF& source) -> ImageF {
        // Crops source
        const ImageF crop = ::crop(source, origin, spotSize);

        ImageF target;
        if(0) {
            // Splits image at spot frequency
            ImageF low_spot = gaussianBlur(crop, 1.f/16*spotSize.x);
            ImageF high = crop - low_spot;
            const float spotLowThreshold = 1.f/8*spotSize.x;
            ImageF low = gaussianBlur(low_spot, spotLowThreshold);
            //ImageF spot = low_spot - low;

            // Blurs divisor to compensate miscalibration
            ImageF divisor = min(attenuation, gaussianBlur(attenuation, spotSize.x/6));

            // Inverses attenuation using attenuation factors calibrated for each pixel
            target = move(low_spot) / divisor;

            // Blurs correction to compensate miscalibration
            gaussianBlur(target, target, spotSize.x/16); // Low pass correction to better correct uniform gradient

            // High pass to match source spot band
            ImageF low_corrected = gaussianBlur(target, spotLowThreshold);
            parallel::sub(target, target, low_corrected);

            if(0) {
                /*// Merges correction near spot
            parallel_apply(target, [=](float low, float target, float spot, float factor, float high) {
                float mixed =
                        correctionFactor < 1 ? mix(spot, min(0.f, target), correctionFactor)
                                             : mix(min(0.f, target), target, correctionFactor-1);
                return low + factor*mixed + (1-factor) * spot + high;
            }, low, target, spot, attenuation, high);*/
            } else {
                // Merges unobscuring correction near spot (i.e saturates any enlightenment to flattening)
                parallel::apply(target, [=](float low, float target, float high) {
                    return low + /*min(0.f, target)*/ target + high;
                }, low, target, high);
            }
        } else {
            // Blurs divisor to compensate miscalibration
            ImageF divisor = share(attenuation); //min(attenuation, gaussianBlur(attenuation, spotSize.x/16));
            // Inverses attenuation using attenuation factors calibrated for each pixel
            target = crop / divisor;
        }

        // Never darkens
        max(target, target, crop);

        // Marks detected spot center (DEBUG)
        //target(spotSize.x/2) = 1;

        // Inserts cropped correction
        return insert(source, target, origin);
    });
}
