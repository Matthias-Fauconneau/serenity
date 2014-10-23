/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "image-group-operation.h"
#include "process.h"
#include "align.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/// Estimates contrast at every pixel
struct Contrast : ImageOperation1, OperationT<Contrast> {
	string name() const override { return "[contrast]"; }
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
			Y(A) = sum ? SAD /*/ sum*/ : 0;
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

/// Converts single component image group to a multiple component image
struct Prism : ImageGroupOperation, OperationT<Prism> {
	string name() const override { return "[prism]"; }
	size_t outputs() const override { return 3; }
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const {
		for(size_t index: range(min(Y.size, X.size))) Y[index].copy(X[index]);
	}
};

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};

	ImageFolder source { folder };
	ProcessedSourceT<Intensity> intensity {source};
	ProcessedSourceT<Normalize> normalize {intensity};
	DifferenceSplit split {normalize};

#if 0
	ProcessedSourceT<LowPass> low {normalize};
	ProcessedImageGroupSource splitLow {low, split};
	ProcessedImageTransformGroupSourceT<Align> transforms {splitLow};
#else
	ProcessedImageGroupSource splitNormalize {normalize, split};
	ProcessedImageTransformGroupSourceT<Align> transforms {splitNormalize};
#endif

	ProcessedImageGroupSource splitSource {source, split};
	TransformSampleImageGroupSource alignSource {splitSource, transforms};
	ProcessedGroupImageGroupSourceT<Intensity> alignIntensity {alignSource};
	ProcessedGroupImageGroupSourceT<Contrast> contrast {alignIntensity};
	ProcessedGroupImageGroupSourceT<LowPass> lowContrast {contrast};
	ProcessedGroupImageGroupSourceT<MaximumWeight> maximumWeights {lowContrast}; // Prevents misalignment blur

	BinaryGroupImageGroupSourceT<Multiply> maximumWeighted {maximumWeights, alignSource};
	ProcessedGroupImageSourceT<Sum> select {maximumWeighted};

	ProcessedGroupImageGroupSourceT<LowPass> lowWeights {maximumWeights}; // Diffuses weight selection
	ProcessedGroupImageGroupSourceT<NormalizeWeights> normalizeWeights {lowWeights};
	BinaryGroupImageGroupSourceT<Multiply> normalizeWeighted {normalizeWeights, alignSource};
	ProcessedGroupImageSourceT<Sum> blend {normalizeWeighted};
};

struct ExposureBlendAnnotate : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBSource sRGB [2] {{source}, {normalize}};
	ImageSourceView views [2] {{sRGB[0], &index}, {sRGB[1], &index}};
	WidgetToggle toggleView {&views[0], &views[1], 0};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(source.elementName(index)); }};

	ExposureBlendAnnotate() {
		for(char c: range('0','9'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
		for(char c: range('a','z'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
	}
	void setCurrentImageAttributes(string currentImageAttributes) {
		imagesAttributes[source.elementName(index)] = String(currentImageAttributes);
	}
};
registerApplication(ExposureBlendAnnotate, annotate);

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	/*ProcessedGroupImageSourceT<Mean> unaligned {splitSource};
	ProcessedGroupImageSourceT<Mean> aligned {alignSource};*/
	ProcessedGroupImageSourceT<Prism> prism {transforms.source};
	TransformSampleImageGroupSource align {transforms.source, transforms};
	ProcessedGroupImageSourceT<Prism> prismAlign {align};

	sRGBSource sRGB [2] {{prism}, {prismAlign}};
	////sRGBSource sRGB [2] {{select}, {blend}};
	ImageSourceView views [2] {{sRGB[0], &index}, {sRGB[1], &index}};

	//sRGBGroupSource sRGB [2] {{splitLow}, {align}};
	//ImageGroupSourceView views [2] {{sRGB[0], &index}, {sRGB[1], &index}};

	WidgetToggle toggleView {&views[0], &views[1], 0};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(source.elementName(index)); }};
};
registerApplication(ExposureBlendPreview);

struct ExposureBlendTest : ExposureBlend, Application {
	ExposureBlendTest() {
		for(size_t groupIndex=0; split.nextGroup(); groupIndex++) {
			auto names = apply(split(groupIndex), [this](const size_t index) { return copy(source.elementName(index)); });
			auto set = apply(names, [this](string name) { return copy(imagesAttributes.at(name)); });
			log(names);
			log(set);
			for(const auto& e: set) assert_(e == set[0]);
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
				'\t',sRGB.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(ExposureBlendExport, export);

struct ExposureBlendSelect : ExposureBlend, Application {
	sRGBSource sRGB {select};
	ExposureBlendSelect() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time correctionTime;
			SourceImageRGB image = sRGB.image(index, int2(2048,1536), true);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name+".select", encodeJPEG(image, 50), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),
				'\t',sRGB.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(ExposureBlendSelect, select);
