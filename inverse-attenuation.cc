#include "inverse-attenuation.h"
#include "parallel.h"

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
		vec2 center = vec2(target.size)/2.f;
		::applyXY(target, [=](float x, float y, float source, float corrected_low_spot, float high) {
			float r2 = sq(vec2(x,y)-center);
			float w = min(1.f, r2/sq(center.x));
			return max(source, w * source + (1-w) * (DC + min(0.f, corrected_low_spot-DC) + high));
		}, crop, target, high);

        // Inserts cropped correction
        return insert(source, target, origin);
    });
}
