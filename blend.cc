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

#if 0
struct MergeGenericImageSource : virtual GenericImageSource {
	GenericImageSource& A;
	GenericImageSource& B;
	Folder cacheFolder {A.name()+B.name(), A.folder()/*FIXME: MRCA of A and B*/, true};
	MergeGenericImageSource(GenericImageSource& A, GenericImageSource& B) : A(A), B(B) {}
	size_t count(size_t need=0) override { assert_(A.count(need) == B.count(need)); return A.count(need); }
	String name() const override { return A.name()+B.name(); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { assert_(A.maximumSize() == B.maximumSize()); return A.maximumSize(); }
	int64 time(size_t index) override { return max(A.time(index), B.time(index)); }
	virtual String elementName(size_t index) const override {
		assert_(A.elementName(index) == B.elementName(index)); return A.elementName(index);
	}
	int2 size(size_t index) const override { assert_(A.size(index) == B.size(index)); return A.size(index); }
};

struct MergeImageGroupSource : MergeGenericImageSource, ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	MergeImageGroupSource(ImageGroupSource& A, ImageGroupSource& B) : MergeGenericImageSource(A, B), A(A), B(B) {}

	size_t outputs() const override { return (A.outputs()?:1)+(B.outputs()?:1); }
	size_t groupSize(size_t groupIndex) const { assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex)); return A.groupSize(groupIndex); }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override {
		size_t subOutputIndex = outputIndex;
		if(subOutputIndex < (A.outputs()?:1)) return A.images(groupIndex, subOutputIndex, size, noCacheWrite);
		subOutputIndex -= (A.outputs()?:1);
		assert_(subOutputIndex < (B.outputs()?:1), outputIndex, A.outputs(), B.outputs());
		return B.images(groupIndex, subOutputIndex, size, noCacheWrite);
	}
};
#endif

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

struct First : ImageGroupOperation1, OperationT<First> {
	string name() const override { return "[first]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		Y.copy(X[0]);
	}
};

/// Sums together all images in an image group
struct Sum : ImageGroupOperation1, OperationT<Sum> {
	string name() const override { return "[sum]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; })); });
	}
};

/// Averages together all images in an image group
struct Mean : ImageGroupOperation1, OperationT<Mean> {
	string name() const override { return "[mean]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
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

	ProcessedGroupImageGroupSourceT<Contrast> weights {alignedIntensity};
	ProcessedGroupImageGroupSourceT<LowPass> lowWeights {weights}; // Filters high frequency noise in contrast estimation
	ProcessedGroupImageGroupSourceT<NormalizeWeights> normalizedWeights {lowWeights};

	BinaryGroupImageGroupSourceT<Multiply> weighted {normalizedWeights, alignedSource};

	ProcessedGroupImageSourceT<First> first {alignedSource};
	ProcessedGroupImageSourceT<Mean> mean {alignedSource};
	ProcessedGroupImageSourceT<Sum> blend {weighted};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBSource sRGB [2] {{first}, {blend}};
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
	ExposureBlendGraph() { log(first.toString()); }
};
registerApplication(ExposureBlendGraph, graph);
