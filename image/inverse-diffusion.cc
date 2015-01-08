#include "inverse-diffusion.h"
#include "parallel.h"

// FIXME: use Intensity operator
static void average(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) {
	parallel::apply(Y, [&](float r, float g, float b) {  return (r+g+b)/3; }, R, G, B);
}

void InverseAttenuation::apply(ref<ImageF> Y, ref<ImageF> X) const {
	const int2 size = X[0].size;

    // Calibrated parameters
    const SourceImage attenuation = Calibration::attenuation(size);
    const int2 spotSize = Calibration::spotSize(size);
    const int2 spotOrigin = Calibration::spotPosition(size)-spotSize/2;

    // Static parameters
	//const float spotLowBound = (spotSize.x-1)/6;
    const float spotHighBound = spotSize.x/16.f;

    int2 origin = spotOrigin;
	// Estimates spot movement
	// Selects [spotLowThreshold - spotHighThreshold] band in cropped intensity image
	ImageF intensity (spotSize);
	average(intensity, copy(crop(X[0], spotOrigin, spotSize)), copy(crop(X[1], spotOrigin, spotSize)), copy(crop(X[2], spotOrigin, spotSize))); // FIXME
	//const ImageF low = gaussianBlur(intensity, spotLowBound);
	intensity = gaussianBlur(intensity, spotHighBound);// - low;

	{float min, max;
	parallel::minmax(intensity, min, max);
	const vec2 center = vec2(intensity.size)/2.f;
	::applyXY(intensity, [=](float x, float y, float v) {
		float r2 = sq(vec2(x,y)-center);
		float w = r2/sq(center.x);
		return w + (v-min)/(max-min);
	}, intensity);}
	const int2 offset = argmin(intensity)-spotSize/2;
	intensity(spotSize/2) = 0; // Marks calibrated spot center (DEBUG)
	intensity(spotSize/2+offset) = 1; // Marks estimated spot center (DEBUG)
	origin = clip(int2(0), spotOrigin+offset, size-spotSize);
	//log(withName(spotOrigin, offset, origin));

    // Image processing
	for(size_t index: range(X.size)) {
		const ImageF& source = X[index];
		//return insert(source, intensity, spotOrigin); // DEBUG: Displays spot tracker field

        // Crops source
        const ImageF crop = ::crop(source, origin, spotSize);

        // Splits details
		ImageF low_spot = gaussianBlur(crop, spotHighBound);
        ImageF high = crop - low_spot;

        // Inverses attenuation (of frequencies under spot frequency) using attenuation factors calibrated for each pixel
		ImageF corrected_low_spot = low_spot / attenuation;

		float DC = parallel::mean(corrected_low_spot);

		// Merges unobscuring correction near spot (i.e saturates any enlightenment to flattening)
		vec2 center = vec2(corrected_low_spot.size)/2.f;
		::applyXY(corrected_low_spot, [=](float x, float y, float source, float corrected_low_spot, float high) {
			float r2 = sq(vec2(x,y)-center);
			float w = min(1.f, r2/sq(center.x));
			return max(source, w * source + (1-w) * (DC + min(0.f, corrected_low_spot-DC) + high));
		}, crop, corrected_low_spot, high);

        // Inserts cropped correction
		const ImageF& target = Y[index];
		insert(target, source, corrected_low_spot, origin);
	}
}
