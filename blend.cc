/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "transform.h"
#include "align.h"
#include "source-view.h"

/// Estimates contrast at every pixel
struct Contrast : ImageOperation1, OperationT<Contrast> {
	string name() const override { return "contrast"; }
	virtual void apply(const ImageF& Y, const ImageF& X) const {
		forXY(Y.size-int2(2), [&](uint x, uint y) {
			int2 p = int2(1) + int2(x,y);
			float sum = 0;
			for(int dy: range(3)) for(int dx: range(3)) sum += abs(X(p) - X(p+int2(dx-1, dy-1)));
			Y(p) = sum;
		});
	}
};

/// Sums together all images in an image group
struct Sum : ImageGroupOperation, OperationT<Sum> {
	string name() const override { return "[sum]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; })); });
	}
};

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Intensity> intensity {source};
	//ProcessedSourceT<BandPass> bandpass {intensity};
	ProcessedSourceT<Normalize> normalize {intensity};
	DifferenceSplit split {normalize};
	ProcessedImageGroupSource sourceSplit {source, split};
	ProcessedImageGroupSource intensitySplit {normalize, split};

	Align align;
	ProcessedImageTransformGroupSource transforms {intensitySplit, align};

	TransformSampleImageGroupSource transformedSource {sourceSplit, transforms};
	TransformSampleImageGroupSource transformedIntensity {intensitySplit, transforms};

	Contrast contrast;
	ProcessedGroupImageGroupSource weights {transformedIntensity, contrast};

	Sum sum;
	ProcessedGroupImageSource aligned {weights, sum};

	sRGBSource sRGB_intensity {intensity};
	sRGBSource sRGB_aligned {aligned};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	/*PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;*/
	size_t index = 0;

	ImageSourceView views [2] {{sRGB_intensity, &index, window}, {sRGB_aligned, &index, window}};
	WidgetToggle toggleView {&views[0], &views[1], 0};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(source.elementName(index)); }};

	ExposureBlendPreview() {
		for(char c: range('0','9'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
	}
	void setCurrentImageAttributes(string currentImageAttributes) {
		imagesAttributes[source.elementName(index)] = String(currentImageAttributes);
	}
};
registerApplication(ExposureBlendPreview);

struct ExposureBlendTest : ExposureBlend, Application {
	ExposureBlendTest() {
		for(size_t groupIndex=0; split.nextGroup(); groupIndex++) {
			log(apply(split(groupIndex), [this](const size_t index) { return copy(imagesAttributes.at(source.elementName(index))); }));
		}
	}
};
registerApplication(ExposureBlendTest, test);
