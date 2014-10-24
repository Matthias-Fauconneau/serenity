/// \file blend.cc Automatic exposure blending
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "multiscale.h"
#include "prism.h"
#include "jpeg-encoder.h"

struct ExposureBlendAnnotate : Application {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };

	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBOperation sRGB {source};
	ImageSourceView view {sRGB, &index};
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

struct JoinOperation : ImageSource {
	array<ImageSource*> sources;
	Folder cacheFolder {"Join", sources[0]->folder() /*FIXME: MRCA*/, true};
	JoinOperation(ref<ImageSource*> sources) : sources(sources) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(apply(sources,[](const ImageSource* source){ return source->name(); })); };
	size_t count(size_t need = 0) override { return sources[0]->count(need); }
	int2 maximumSize() const override { return sources[0]->maximumSize(); }
	String elementName(size_t index) const override { return sources[0]->elementName(index); }
	int64 time(size_t index) override { return max(apply(sources,[=](ImageSource* source){ return source->time(index); })); }
	int2 size(size_t index) const override { return sources[0]->size(index); }

	size_t outputs() const override { return sources.size; }
	SourceImage image(size_t index, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override {
		assert_(sources[componentIndex]->outputs()==1);
		log(sources[componentIndex]->folder().name());
		return sources[componentIndex]->image(index, 0, size, noCacheWrite);
	}
};

struct NormalizeMinMax : ImageOperator, OperatorT<NormalizeMinMax> {
	//string name() const override { return "NormalizeMinMax"; }
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> Xx /*minmax, source*/) const override {
		assert_(Xx.size==2 && Y.size==1);
		ref<ImageF> X = Xx.slice(0, Xx.size-1);
		const ImageF& x = Xx.last();
		float min=inf, max=-inf;
		for(const ImageF& x: X) parallel::minmax(x, min, max);
		assert_(max > min);
		//for(size_t index: range(Y.size)) parallel::apply(Y[index], [min, max](float x) { return (x-min)/(max-min); }, X[index]);
		parallel::apply(Y[0], [min, max](float x) { return (x-min)/(max-min); }, x);
	}
};

/*// Brightens low octaves so higher octaves are not clipped below zero
struct LowPlus : ImageOperator1, OperatorT<LowPlus> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	void apply(const ImageF& Y, const ImageF& X) const override {
		const float largeScale = (X.size.y-1)/6;
		Y.copy(X);
		for(float octave=largeScale; octave>=largeScale/128; octave/=2) {
			auto low = gaussianBlur(Y, octave);
			auto lowmid = gaussianBlur(Y, octave/2);
			auto band = lowmid - low;
			float min = parallel::min(band);
			assert_(min < 0);
			parallel::apply(Y, [min](float x, float low) {
				float high = x-low;
				return max(-min, low)+high;
			}, Y, low);
		}
	}
};*/

struct DCHigh : ImageOperator1, OperatorT<DCHigh> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	void apply(const ImageF& Y, const ImageF& X) const override {
		const float largeScale = (X.size.y-1)/6;
		auto low = gaussianBlur(X, largeScale/4);
		float DC = parallel::mean(X);
		parallel::apply(Y, [DC](float x, float low) {
			float high = x - low;
			return DC + high;
		}, X, low);
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
	//ImageGroupOperationT<Intensity> alignIntensity {alignSource};
	ImageGroupOperationT<Weight> weights {alignSource};
	ImageGroupOperationT<LowPass> lowWeights {weights};

	ImageGroupOperationT<SelectMaximum> maximumWeights {lowWeights};
	BinaryImageGroupOperationT<Multiply> weighted {maximumWeights, alignSource};
	ImageGroupFoldT<Sum> select {weighted};

	ImageGroupOperationT<WeightFilterBank> weightBands {weights/*maximumWeights*/}; // Splits each weight selection in bands
	ImageGroupOperationT<SelectMaximum> maximumWeightBands {weightBands};
	ImageGroupOperationT<WeightFilterBank2> weightBands2 {maximumWeightBands}; // Lowpass each weight selection in bands
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {weightBands2}; // Normalizes weight selection for each band

	ImageGroupOperationT<Index0> alignB {alignSource};
	ImageGroupOperationT<FilterBank> bBands {alignB};
	BinaryImageGroupOperationT<Multiply> multiBandWeightedB {normalizeWeightBands, bBands}; // Applies
	ImageGroupOperationT<Sum> joinB {multiBandWeightedB}; // Joins bands again
	ImageGroupFoldT<Sum> blendB {joinB}; // Blends images

	ImageGroupOperationT<Index1> alignG {alignSource};
	ImageGroupOperationT<FilterBank> gBands {alignG};
	BinaryImageGroupOperationT<Multiply> multiBandWeightedG {normalizeWeightBands, gBands}; // Applies
	ImageGroupOperationT<Sum> joinG {multiBandWeightedG}; // Joins bands again
	ImageGroupFoldT<Sum> blendG {joinG}; // Blends images

	ImageGroupOperationT<Index2> alignR {alignSource};
	ImageGroupOperationT<FilterBank> rBands {alignR};
	BinaryImageGroupOperationT<Multiply> multiBandWeightedR {normalizeWeightBands, rBands}; // Applies
	ImageGroupOperationT<Sum> joinR {multiBandWeightedR}; // Joins bands again
	ImageGroupFoldT<Sum> blendR {joinR}; // Blends images

	JoinOperation blend {{&blendB, &blendG, &blendR}};

	//ImageOperationT<DCHigh> output {blend};

	/*ImageOperationT<DCHigh> high {blend};
	ImageOperationT<Intensity> highY {high};
	ImageOperationT<LowPass> lowY {highY};
	BinaryImageOperationT<NormalizeMinMax> normalizeMinMax {lowY, high};
	ImageSource& output = normalizeMinMax;*/

	/*ImageOperationT<LowPlus> lowPlus {blend};
	ImageSource& output = lowPlus;*/

	/*ImageOperationT<HighPass> high {blend};
	ImageOperationT<Intensity> highY {high};
	ImageOperationT<LowPass> lowHighY {highY};
	BinaryImageOperationT<NormalizeMinMax> output {lowHighY, high};*/

	/*ImageOperationT<Intensity> blendY {blend};
	ImageOperationT<LowPass> lowBlendY {blendY};
	BinaryImageOperationT<NormalizeMinMax> normalizeMinMax {lowBlendY, blend};
	ImageSource& output = normalizeMinMax;*/

	ImageOperationT<Intensity> blendY {blend};
	ImageOperationT<LowPass> lowBlendY {blendY};
	BinaryImageOperationT<NormalizeMinMax> normalizeMinMax {lowBlendY, blend};
	ImageSource& output = normalizeMinMax;
};

struct Transpose : OperatorT<Transpose> { /*string name() const override { return  "Transpose"; }*/ };
/// Swaps component and group indices
struct TransposeOperation : GenericImageOperation, ImageGroupSource, OperatorT<Transpose> {
	ImageGroupSource& source;
	TransposeOperation(ImageGroupSource& source) : GenericImageOperation(source, *this), source(source) {}

	size_t outputs() const override { return source.groupSize(0); /*Assumes all groups have same size*/ }
	size_t groupSize(size_t) const { return source.outputs(); }
	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite = false) {
		array<SourceImage> outputs;
		assert_(source.outputs(), source.toString());
		for(size_t outputIndex: range(source.outputs())) {
			auto inputs = source.images(groupIndex, outputIndex, size, noCacheWrite);
			assert_(componentIndex < inputs.size);
			outputs.append( move(inputs[componentIndex]) );
		}
		return outputs;
	}
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageGroupFoldT<Prism> prismWeights0 {weights};
	ImageGroupFoldT<Prism> prismWeights1 {lowWeights};
	ImageGroupFoldT<Prism> prismWeights2 {maximumWeights};
	//sRGBOperation sRGB [3] {blend, output, select/*, prismWeights1, prismWeights0*/};
	sRGBOperation sRGB [3] {prismWeights0, prismWeights1, prismWeights2};
	//sRGBOperation sRGB [1] {prismWeights0};
	array<ImageSourceView> sRGBView = apply(mref<sRGBOperation>(sRGB),
											[&](ImageRGBSource& source) -> ImageSourceView { return {source, &index}; });
	TransposeOperation transposeWeightBands0 {weightBands};
	TransposeOperation transposeWeightBands1 {normalizeWeightBands};
	ImageGroupOperationT<Prism> prism [2] {transposeWeightBands0, transposeWeightBands1};
	sRGBGroupOperation sRGBGroups [2] {{prism[0]}, prism[1]};
	array<ImageGroupSourceView> sRGBGroupView = apply(mref<sRGBGroupOperation>(sRGBGroups),
											[&](sRGBGroupOperation& source) -> ImageGroupSourceView { return {source, &index}; });
	WidgetCycle view {toWidgets(sRGBView)+toWidgets(sRGBGroupView)};
	Window window {&view, -1, [this]{ return view.title()+" "+imagesAttributes.value(source.elementName(index)); }};
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

/*struct ExposureBlendExport : ExposureBlend, Application {
	sRGBOperation sRGB {output};
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
registerApplication(ExposureBlendSelect, select);*/
