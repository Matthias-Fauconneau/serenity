/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/// Estimates contrast at every pixel
struct Contrast : ImageOperator1, OperatorT<Contrast> {
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

struct MaximumWeight : ImageGroupOperator, OperatorT<MaximumWeight> {
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
struct NormalizeWeights : ImageGroupOperator, OperatorT<NormalizeWeights> {
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

#include "color.h"
/// Converts single component image group to a multiple component image
struct Prism : ImageGroupOperator, OperatorT<Prism> {
	string name() const override { return "[prism]"; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		if(X.size <= 3) {
			for(size_t index: range(X.size)) Y[index].copy(X[index]);
			for(size_t index: range(X.size, Y.size)) Y[index].clear();
		} else {
			assert_(Y.size == 3);
			for(size_t index: range(X.size)) {
				vec3 color = LChuvtoBGR(53, 179, 2*PI*index/X.size);
				assert_(isNumber(color));
				for(size_t component: range(3)) {
					assert_(X[index].size == Y[component].size);
					if(index == 0) parallel::apply(Y[component], [=](float x) { return x * color[component]; }, X[index]);
					else parallel::apply(Y[component], [=](float y, float x) { return y + x * color[component]; }, Y[component], X[index]);
				}
			}
			for(size_t component: range(3)) for(auto v: Y[component]) assert_(isNumber(v));
		}
	}
};

/// Splits in bands
struct FilterBank : ImageOperator, OperatorT<FilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 2; }
	string name() const override { return "[filterbank]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(X.size == 1);
		const int bandCount = Y.size;
		const ImageF& r = Y[bandCount-1];
		r.copy(X[0]); // Remaining octaves
		const float largeScale = (min(r.size.x,r.size.y)-1)/6;
		float octaveScale = largeScale / exp2(bandCount-1);
		for(size_t index: range(bandCount-1)) {
			gaussianBlur(Y[index], r, octaveScale); // Splits lowest octave
			parallel::sub(r, r, Y[index]); // Removes from remainder
			octaveScale *= 2; // Next octave
		}
		// Y[bandCount-1] holds remaining high octaves
	}
};

/// Applies weights bands to source bands
struct MultiBandWeight : ImageOperator, OperatorT<MultiBandWeight> {
	size_t inputs() const override { return 0; }
	size_t outputs() const override { return 1; }
	string name() const override { return "[multibandweight]"; }
	void apply(ref<ImageF> Y, ref<ImageF> X /*weight bands, source*/) const override {
		const int bandCount = X.size-1;
		assert_(X.size > 2 && Y.size == 1, X.size, Y.size);
		ImageF r = copy(X.last()); // Remaining source octaves
		const float largeScale = (min(r.size.x,r.size.y)-1)/6;
		float octaveScale = largeScale / exp2(bandCount-1);
		for(size_t index: range(bandCount-1)) {
			ImageF octave = gaussianBlur(r, octaveScale); // Splits lowest source octave
			if(index==0) parallel::mul(Y[0], X[index], octave); // Adds octave contribution
			else parallel::fmadd(Y[0], X[index], octave); // Adds octave contribution
			parallel::sub(r, r, Y[index]); // Removes from remainder
			octaveScale *= 2; // Next octave
		}
		parallel::fmadd(Y[0], X[bandCount-1], r); // Applies band weights remainder to source remainder
	}
};

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};

	ImageFolder source { folder };
	ImageOperationT<Intensity> intensity {source};
	ImageOperationT<Normalize> normalize {intensity};
	DifferenceSplit split {normalize};

	GroupImageOperation splitNormalize {normalize, split};
	ImageGroupTransformOperationT<Align> transforms {splitNormalize};

	GroupImageOperation splitSource {source, split};
	SampleImageGroupOperation alignSource {splitSource, transforms};
	ImageGroupOperationT<Intensity> alignIntensity {alignSource};
	ImageGroupOperationT<Contrast> contrast {alignIntensity};
	ImageGroupOperationT<MaximumWeight> maximumWeights {contrast}; // Selects maximum weights

	BinaryImageGroupOperationT<Multiply> maximumWeighted {maximumWeights, alignSource};
	ImageGroupFoldT<Sum> select {maximumWeighted};

	ImageGroupOperationT<FilterBank> weightBands {maximumWeights}; // Splits each weight selection in bands
	ImageGroupOperationT<NormalizeWeights> normalizeWeightBands {weightBands}; // Normalizes weight selection for each band
	BinaryImageGroupOperationT<MultiBandWeight> multiBandWeighted {normalizeWeightBands, alignSource};
	ImageGroupFoldT<Sum> blend {multiBandWeighted};
};

struct ExposureBlendAnnotate : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBOperation sRGB [2] {{source}, {normalize}};
	ImageSourceView views [2] {{sRGB[0], &index}, {sRGB[1], &index}};
	WidgetCycle view {views};
	Window window {&view, -1, [this]{ return view.title()+" "+imagesAttributes.value(source.elementName(index)); }};

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

	/*ImageGroupFoldT<Prism> prism {transforms.source};
	SampleImageGroupOperation align {transforms.source, transforms};
	ImageGroupFoldT<Prism> prismAlign {align};
	sRGBOperation sRGB [4] {{prism}, {prismAlign},{select}, {blend}};
	ImageSourceView views [4] {{sRGB[0], &index}, {sRGB[1], &index}, {sRGB[2], &index}, {sRGB[3], &index}};*/

	ImageGroupFoldT<Prism> prismMaximumWeights {maximumWeights};
	sRGBOperation sRGBs [1] {{prismMaximumWeights}};

	ImageGroupOperationT<Prism> prismWeightBands {weightBands};
	sRGBImageGroupSource sRGBGroups [1] {{prismWeightBands}};

	//sRGBGroupImageSource sRGB [2] {{splitLow}, {align}};
	//ImageGroupSourceView views [2] {{sRGB[0], &index}, {sRGB[1], &index}};

	ImageSourceView sRGBViews [1] {{sRGBs[0], &index}};
	ImageGroupSourceView sRGBGroupViews [1] {{sRGBGroups[0], &index}};

	//WidgetCycle view {{&views[3]}};
	WidgetCycle view {{&sRGBViews[0], &sRGBGroupViews[0]}};
	Window window {&view, -1, [this]{ return view.title()+" "+imagesAttributes.value(source.elementName(index)); }};

	ExposureBlendPreview() { log(sRGBGroups[0].toString()); }
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
	sRGBOperation sRGB {blend};
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
	sRGBOperation sRGB {select};
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
