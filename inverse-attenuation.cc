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

    // Calibrated parameters
    const SourceImage attenuation = Calibration::attenuation(size);
    const int2 spotSize = Calibration::spotSize(size);
    const int2 spotOrigin = Calibration::spotPosition(size)-spotSize/2;

    // Static parameters
    const float lowScale = (spotSize.x-1)/6;
    const float highScale = spotSize.x/16.f;

    // Estimated parameters
    int2 origin = spotOrigin;
    if(1) {
        ImageF value = crop(red, green, blue, spotOrigin, spotSize);
        const ImageF low = gaussianBlur(value, lowScale);
        /*// Splits image frequencies to detect uniform background under spot
    const float lowEnergy = parallel::energy(low);
    const float highEnergy = parallel::sum(value, [](float value, float low) { return sq(value-low); }, 0.f, low);
    assert_(lowEnergy > 0 && highEnergy > 0, lowEnergy, highEnergy);
    const float ratio = lowEnergy / highEnergy;
    const float blurRadius = ::min(ratio / 32, 1.f) * lowScale;*/

        value = gaussianBlur(value, highScale) - low; // Band pass [spotLowThreshold - spotHighThreshold]
        const float min = parallel::min(value);
        const float max = parallel::max(value);
        const vec2 center = vec2(value.size)/2.f;
        ::apply(value, [=](float x, float y, float v) {
            float r2 = sq(vec2(x,y)-center);
            float w = r2/sq(center.x);
            return w + (v-min)/(max-min);
        });
        const int2 offset = argmin(value)-spotSize/2;
        value(spotSize/2) = 0;    // Marks calibrated spot center (DEBUG)
        value(spotSize/2+offset) = 1;     // Marks estimated spot center (DEBUG)
        origin = clip(int2(0), spotOrigin+offset, size-spotSize); // Compenstates dust movement
        //log(withName(spotOrigin, offset, origin, spotSize.x, lowEnergy, highEnergy, ratio, lowScale, blurRadius));
    }

    // Image processing
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [&](const ImageF& source) -> ImageF {
        //return insert(source, value, spotOrigin); // DEBUG: view offset tracker field

        // Crops source
        const ImageF crop = ::crop(source, origin, spotSize);

        // Splits details
        ImageF low_mid = gaussianBlur(crop, highScale);
        ImageF high = crop - low_mid;

        // Inverses attenuation (of frequencies under spot frequency) using attenuation factors calibrated for each pixel
        ImageF target = low_mid / attenuation;

        float DC = parallel::mean(target);

        // Merges unobscuring correction near spot (i.e saturates any enlightenment to flattening)
        vec2 center = vec2(target.size)/2.f;
        ::apply(target, [=](float x, float y, float, float source, float corrected_low_mid, float high) {
            float r2 = sq(vec2(x,y)-center);
            float w = ::min(1.f, r2/sq(center.x));
            return ::max(source, w * source + (1-w) * (DC + ::min(0.f, corrected_low_mid-DC) + high));
        }, crop, target, high);

        // Inserts cropped correction
        return insert(source, target, origin);
    });
}
