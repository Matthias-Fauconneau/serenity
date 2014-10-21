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

/*struct MergeImageSource : MergeGenericImageSource, ImageSource {
	size_t outputs() const override { return A.outputs()+B.outputs(); }
	SourceImage image(size_t index, size_t outputIndex, int2 unused size=0, bool unused noCacheWrite = false) override {
		if(outputIndex < A.outputs()) return A.image(index, outputIndex, size, noCacheWrite);
		else return B.image(index, outputIndex - A.outputs(), size, noCacheWrite);
	}
};*/

struct MergeImageGroupSource : MergeGenericImageSource, ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	MergeImageGroupSource(ImageGroupSource& A, ImageGroupSource& B) : MergeGenericImageSource(A, B), A(A), B(B) {}

	size_t outputs() const override { return A.outputs()+B.outputs(); }
	size_t groupSize(size_t groupIndex) const { assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex)); return A.groupSize(groupIndex); }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override {
		size_t subOutputIndex = outputIndex;
		if(subOutputIndex < A.outputs()) return A.images(groupIndex, subOutputIndex, size, noCacheWrite);
		subOutputIndex -= A.outputs();
		assert_(subOutputIndex < B.outputs(), outputIndex, A.outputs(), B.outputs());
		return B.images(groupIndex, subOutputIndex, size, noCacheWrite);
	}
};

struct RepeatMergeImageGroupSource : MergeGenericImageSource, ImageGroupSource {
	ImageGroupSource& A;
	ImageSource& B;
	RepeatMergeImageGroupSource(ImageGroupSource& A, ImageSource& B) : MergeGenericImageSource(A, B), A(A), B(B) {}

	size_t outputs() const override { return A.outputs()+B.outputs(); }
	size_t groupSize(size_t groupIndex) const { return A.groupSize(groupIndex); }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override {
		size_t subOutputIndex = outputIndex;
		if(subOutputIndex < A.outputs()) return A.images(groupIndex, subOutputIndex, size, noCacheWrite);
		subOutputIndex -= A.outputs();
		assert_(subOutputIndex < B.outputs(), outputIndex, A.outputs(), B.outputs());
		size_t groupSize = this->groupSize(groupIndex);
		array<SourceImage> b (groupSize);
		b.append( B.image(groupIndex, subOutputIndex, size, noCacheWrite) );
		while(b.size < groupSize) b.append(share(b.first()));
		return b;
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

	ProcessedImageTransformGroupSourceT<Align> transforms {intensitySplit};

	TransformSampleImageGroupSource alignedSource {sourceSplit, transforms};
	ProcessedGroupImageGroupSourceT<Intensity> alignedIntensity {alignedSource};

	ProcessedGroupImageGroupSourceT<Contrast> weights {alignedIntensity};

	ProcessedGroupImageSourceT<Sum> weightSum {weights};

	RepeatMergeImageGroupSource weights_weightSum{weights, weightSum};

	ProcessedGroupImageGroupSourceT<Divide> normalizedWeights {weights_weightSum};

	MergeImageGroupSource weights_source {normalizedWeights, alignedIntensity /*alignedSource*/};
	ProcessedGroupImageGroupSourceT<Multiply> weighted {weights_source};

	ProcessedGroupImageSourceT<Sum> aligned {weighted};

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
