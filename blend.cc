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
			float sum = 0, SAD = 0;
			float a = X(p);
			for(int dy: range(3)) for(int dx: range(3)) {
				float b = X(p+int2(dx-1, dy-1));
				sum += b;
				SAD += abs(a - b);
			}
			Y(p) = sum ? SAD / sum : 0;
		});
	}
};

/// Normalizes weights
/// \note if all weights are zero, weights are all set to 1/groupSize.
struct NormalizeWeights : ImageGroupOperation, OperationT<NormalizeWeights> {
	string name() const override { return "[normalize-weights]"; }
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == X.size);
		forXY(Y[0].size, [&](uint x, uint y) {
			float sum = 0;
			for(size_t index: range(X.size)) sum += X[index](x, y);
			if(sum) for(size_t index: range(Y.size)) Y[index](x, y) = X[index](x, y)/sum;
			else for(size_t index: range(Y.size)) Y[index](x, y) = 1./X.size;
		});
	}
};

struct MaximumWeight : ImageGroupOperation, OperationT<MaximumWeight> {
	string name() const override { return "[maximum-weight]"; }
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == X.size);
		forXY(Y[0].size, [&](uint x, uint y) {
			int best = -1; float max = 0;
			for(size_t index: range(X.size)) { float v = X[index](x, y); if(v > max) { max = v, best = index; } }
			for(size_t index: range(Y.size)) Y[index](x, y) = 0;
			if(best>=0) Y[best](x, y) = 1;
		});
	}
};


struct First : ImageGroupOperation1, OperationT<First> {
	string name() const override { return "[first]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		Y.copy(X[0]);
	}
};

/// Averages together all images in an image group
struct Mean : ImageGroupOperation1, OperationT<Mean> {
	string name() const override { return "[mean]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
	}
};

/// Sums together all images in an image group
struct Sum : ImageGroupOperation1, OperationT<Sum> {
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

	ProcessedImageTransformGroupSourceT<Align> transforms {intensitySplit};

	TransformSampleImageGroupSource alignedSource {sourceSplit, transforms};
	ProcessedGroupImageGroupSourceT<Intensity> alignedIntensity {alignedSource};

	ProcessedGroupImageGroupSourceT<Contrast> contrast {alignedIntensity};
	ProcessedGroupImageGroupSourceT<LowPass> lowWeights {contrast}; // Filters high frequency noise in contrast estimation
	ProcessedGroupImageGroupSourceT<MaximumWeight> maximumWeights {lowWeights}; // Prevents ghosting from misalignment
	ProcessedGroupImageGroupSourceT<NormalizeWeights> normalizeWeights {lowWeights};

	BinaryGroupImageGroupSourceT<Multiply> maximumWeighted {maximumWeights, alignedSource};
	BinaryGroupImageGroupSourceT<Multiply> normalizeWeighted {normalizeWeights, alignedSource};

	ProcessedGroupImageSourceT<First> first {alignedSource};
	ProcessedGroupImageSourceT<Sum> mask {maximumWeighted};
	ProcessedGroupImageSourceT<Sum> blend {normalizeWeighted};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBSource sRGB [2] {{mask}, {blend}};
	ImageSourceView views [2] {{sRGB[0], &index, window}, {sRGB[1], &index, window}};
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

struct ExposureBlendGraph : ExposureBlend, Application {
	ExposureBlendGraph() { log(mask.toString()); }
};
registerApplication(ExposureBlendGraph, graph);
