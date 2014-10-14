#include "inverse-attenuation.h"
#include "parallel.h"
//#include "math.h"

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
    const float spotLowBound = (spotSize.x-1)/6;
    const float spotHighBound = spotSize.x/16.f;

    int2 origin = spotOrigin;
    if(1) { // Estimates spot movement

        // Selects [spotLowThreshold - spotHighThreshold] band in cropped intensity image
        ImageF intensity = crop(red, green, blue, spotOrigin, spotSize);
        const ImageF low = gaussianBlur(intensity, spotLowBound);
        intensity = gaussianBlur(intensity, spotHighBound) - low;

        float min, max;
        parallel::minmax(intensity, min, max);
        const vec2 center = vec2(intensity.size)/2.f;
        ::applyXY(intensity, [=](float x, float y, float v) {
            float r2 = sq(vec2(x,y)-center);
            float w = r2/sq(center.x);
            return w + (v-min)/(max-min);
        }, intensity);
        const int2 offset = argmin(intensity)-spotSize/2;
        intensity(spotSize/2) = 0; // Marks calibrated spot center (DEBUG)
        intensity(spotSize/2+offset) = 1; // Marks estimated spot center (DEBUG)
        origin = clip(int2(0), spotOrigin+offset, size-spotSize);
        //log(withName(spotOrigin, offset, origin));
    }

    // Image processing
    return ::apply(ref<ImageF>{share(red), share(green), share(blue)}, [&](const ImageF& source) -> ImageF {
        //return insert(source, intensity, spotOrigin); // DEBUG: Displays spot tracker field

        // Crops source
        const ImageF crop = ::crop(source, origin, spotSize);

        // Splits details
        ImageF low_spot = gaussianBlur(crop, spotHighBound);
        ImageF high = crop - low_spot;

        // Inverses attenuation (of frequencies under spot frequency) using attenuation factors calibrated for each pixel
        ImageF target = low_spot / attenuation;

        float DC = parallel::mean(target);

        // Merges unobscuring correction near spot (i.e saturates any enlightenment to flattening)
        ::apply(target, [=](float source, float corrected_low_spot, float high) {
            return max(source, DC + min(0.f, corrected_low_spot-DC) + high);
        }, crop, target, high);

        // Inserts cropped correction
        return insert(source, target, origin);
    });
}
