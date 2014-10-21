/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "transform.h"
#include "align.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/// Estimates contrast at every pixel
struct Contrast : ImageOperation1, OperationT<Contrast> {
	string name() const override { return "contrast"; }
	virtual void apply(const ImageF& Y, const ImageF& X) const {
		forXY(Y.size, [&](uint x, uint y) {
			int2 A = int2(x,y);
			float sum = 0, SAD = 0;
			float a = X(A);
			for(int dy: range(3)) for(int dx: range(3)) {
				int2 B = A+int2(dx-1, dy-1);
				if(!(B >= int2(0) && B < X.size)) continue;
				float b = X(B);
				sum += b;
				SAD += abs(a - b);
			}
			Y(A) = sum ? SAD / sum : 0;
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

struct Mask : ImageOperation21, OperationT<Mask> {
	string name() const override { return "[mask]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const override;
};
inline void Mask::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const {
	::apply(Y, [&](float a, float b) { return a ? b : 0; }, X0, X1);
}

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
	ProcessedSourceT<Normalize> normalize {intensity};
	DifferenceSplit split {normalize};
	ProcessedImageGroupSource sourceSplit {source, split};
	ProcessedImageGroupSource intensitySplit {normalize, split};
	ProcessedImageTransformGroupSourceT<Align> transforms {intensitySplit};
	TransformSampleImageGroupSource alignedSource {sourceSplit, transforms};
	ProcessedGroupImageSourceT<Mean> mean {alignedSource};
	ProcessedGroupImageGroupSourceT<Intensity> alignedIntensity {alignedSource};
	ProcessedGroupImageGroupSourceT<Contrast> contrast {alignedIntensity};
	ProcessedGroupImageGroupSourceT<LowPass> lowContrast {contrast};
	ProcessedGroupImageGroupSourceT<MaximumWeight> maximumWeights {lowContrast}; // Prevents misalignment blur
	ProcessedGroupImageGroupSourceT<LowPass> lowWeights {maximumWeights}; // Diffuses weight selection
	BinaryGroupImageGroupSourceT<Mask> maskLowWeights {alignedIntensity, lowWeights}; // Clears weight where no data is available
	ProcessedGroupImageGroupSourceT<NormalizeWeights> normalizeWeights {maskLowWeights};
	BinaryGroupImageGroupSourceT<Multiply> normalizeWeighted {normalizeWeights, alignedSource};
	ProcessedGroupImageSourceT<Sum> blend {normalizeWeighted};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBSource sRGB [2] {{mean}, {blend}};
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

struct ExposureBlendExport : ExposureBlend, Application {
	sRGBSource sRGB {blend};
	ExposureBlendExport() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time correctionTime;
			SourceImageRGB image = sRGB.image(index, int2(2048,1536), true);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image, 50), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),
				'\t',imagesAttributes[sRGB.elementName(index)],
				'\t',sRGB.elementName(index), strx(sRGB.size(index)),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(ExposureBlendExport, export);

