/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "transform.h"
#include "align.h"
#include "source-view.h"

/// Merges components from two sources
#if 0
struct MergeImageGroupSource : ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	Folder cacheFolder {A.name()+B.name(), A.folder()/*FIXME: MRCA of A and B*/, true};
	MergeImageGroupSource(ImageGroupSource& A, ImageGroupSource& B) : A(A), B(B) {}
	size_t count(size_t need=0) override { assert_(A.count(need) == B.count(need)); return A.count(need); }
	String name() const override { return A.name()+B.name(); }
	int outputs() const override { return A.outputs()+B.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { assert_(A.maximumSize() == B.maximumSize()); return A.maximumSize(); }
	int64 time(size_t index) override { return max(A.time(index), B.time(index)); }
	virtual String elementName(size_t index) const override {
		assert_(A.elementName(index) == B.elementName(index)); return A.elementName(index);
	}
	int2 size(size_t index) const override { assert_(A.size(index) == B.size(index)); return A.size(index); }

	array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) override {
		if(outputIndex < A.outputs()) return A.images(groupIndex, outputIndex, size, noCacheWrite);
		else return B.images(groupIndex, outputIndex - A.outputs(), size, noCacheWrite);
	}
};
#else
struct MergeImageSource : ImageSource {
	ImageSource& A;
	ImageSource& B;
	Folder cacheFolder {A.name()+B.name(), A.folder()/*FIXME: MRCA of A and B*/, true};
	MergeImageSource(ImageSource& A, ImageSource& B) : A(A), B(B) {}
	size_t count(size_t need=0) override { assert_(A.count(need) == B.count(need)); return A.count(need); }
	String name() const override { return A.name()+B.name(); }
	int outputs() const override { return A.outputs()+B.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { assert_(A.maximumSize() == B.maximumSize()); return A.maximumSize(); }
	int64 time(size_t index) override { return max(A.time(index), B.time(index)); }
	virtual String elementName(size_t index) const override {
		assert_(A.elementName(index) == B.elementName(index)); return A.elementName(index);
	}
	int2 size(size_t index) const override { assert_(A.size(index) == B.size(index)); return A.size(index); }

	SourceImage image(size_t index, int outputIndex, int2 unused size=0, bool unused noCacheWrite = false) override {
		if(outputIndex < A.outputs()) return A.image(index, outputIndex, size, noCacheWrite);
		else return B.image(index, outputIndex - A.outputs(), size, noCacheWrite);
	}
};
#endif

// FIXME: Selects same image for all channels (estimate contrast on intensity)
struct ContrastBlend : ImageGroupOperation, OperationT<ContrastBlend> {
	string name() const override { return "[blend]"; }
	virtual int inputs() const { return 4; }
	virtual int outputs() const { return 3; }
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const {
		assert_(X.size%4 == 0);
		size_t imageCount = X.size/4;
		forXY(Y[0].size-int2(2), [&](uint x, uint y) {
			int2 p = int2(1) + int2(x,y);
			uint best = 0; float bestContrast = 0;
			for(size_t index: range(imageCount)) {
				const float kernel[3*3] = {1./2, 1, 1./2,  1,-6,1,  1./2, 1, 1./2};
				float sum = 0;
				for(int dy: range(3)) for(int dx: range(3)) sum += kernel[dy*3+dx] * X[index](p+int2(dx-1, dy-1));
				if(sum > bestContrast) {
					best = index;
					bestContrast = sum;
				}
			}
			assert_(best < X.size);
			for(uint outputIndex: range(3)) Y[outputIndex](p) = X[outputIndex*imageCount+best](p);
		});
	}
};

/// Evaluates transforms to align groups of images

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Intensity> intensity {source};
	MergeImageSource intensitySource {intensity, source};
	ProcessedSourceT<BandPass> bandpass {intensity};
	ProcessedSourceT<Normalize> normalize {bandpass};
	DifferenceSplit split {normalize};
	ProcessedImageGroupSource sourceSplit {source, split};
	ProcessedImageGroupSource intensitySplit {normalize, split};
	ProcessedImageGroupSource intensitySourceSplit {intensitySource, split};

	Align align;
	ProcessedImageTransformGroupSource transforms {intensitySplit, align};

	TransformSampleImageGroupSource transformed {intensitySourceSplit, transforms};

	ContrastBlend blend;
	//ProcessedGroupImageSource unaligned {intensitySourceSplit, blend};
	ProcessedGroupImageSource aligned {transformed, blend};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	/*PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;*/
	size_t index = 0;

#if 0
	ImageSourceView views [2] {{unaligned, &index, window, source.maximumSize()/32}, {aligned, &index, window, source.maximumSize()/32}};
#else
	ImageSourceView views [2] {{source, &index, window}, {aligned, &index, window}};
#endif
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
