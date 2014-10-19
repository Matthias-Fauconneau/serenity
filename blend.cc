/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "processed-source.h"
#include "image-source-view.h"
#include "image-folder.h"
using namespace parallel;

/// Sums \a R+\a G+\a B into \a Y
static void sum(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) { apply(Y, [&](float r, float g, float b) {  return r+g+b; }, R, G, B); }
inline ImageF sum(ImageF&& y, const ImageF& r, const ImageF& g, const ImageF& b) { sum(y, r, g, b); return move(y); }
inline ImageF sum(const ImageF& r, const ImageF& g, const ImageF& b) { return sum({r.size,"min"}, r, g, b); }

/// Normalizes mean and deviation
struct Normalize : ImageOperationT<Normalize> {
	int type() const override { return 1; }
	ImageF apply1(const ImageF& red, const ImageF& green, const ImageF& blue) const override {
		const int2 size = red.size; assert_(green.size == size && blue.size == size);

		ImageF intensity = ::sum(red, green, blue);
		const float largeScale = (intensity.size.y-1)/6;
		gaussianBlur(intensity, intensity, largeScale/2); // Low pass [ .. largeScale/2]
		sub(intensity, intensity, gaussianBlur(intensity, largeScale)); // High pass [largeScale .. ]

		//float mean = parallel::mean(intensity);
		//float energy = reduce(intensity, [mean](float accumulator, float value) { return accumulator + sq(value-mean); }, 0.f);
		float energy = parallel::energy(intensity);
		float deviation = sqrt(energy / intensity.buffer::size);

		parallel::apply(intensity, [/*mean, */deviation](const float value) { return (1+(value/*-mean*/)/deviation /*-1,1*/)/2 /*0,1*/; }, intensity);
		return intensity;
	}
};

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	Normalize process;

	ImageFolder source { folder };
	ProcessedSource processed {source, process};
};

struct ExposureBlendlPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.name(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageSourceView sourceView {source, &index, window};
	ImageSourceView processedView {processed, &index, window};
	WidgetToggle toggleView {&sourceView, &processedView};
	Window window {&toggleView};
};
registerApplication(ExposureBlendlPreview);
