/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "multiscale.h"
#include "prism.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/*/// Multiplies 2 components together
struct MultiplyAdd : ImageOperator, OperatorT<Multiply> {
	string name() const override { return "[multiply]"; }
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override { apply(Y[0], X[0], X[1]); }
};
void Multiply::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { parallel::mul(Y, X0, X1); }*/

struct Join : OperatorT<Join> {
	string name() const override { return  "[join]"; }
};
struct JoinOperation : GenericImageOperation, ImageSource, Join {
	array<ImageSource*> sources;
	JoinOperation(ref<ImageSource*> sources) : GenericImageOperation(*sources[0], *this), sources(sources) {}

	size_t outputs() const override { return sources.size; }
	SourceImage image(size_t index, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override {
		assert_(sources[componentIndex]->outputs()==1);
		return sources[componentIndex]->image(index, 0, size, noCacheWrite);
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

	ImageGroupOperationT<WeightFilterBank> weightBands {maximumWeights}; // Splits each weight selection in bands
	ImageGroupOperationT<NormalizeWeights> normalizeWeightBands {weightBands}; // Normalizes weight selection for each band
	//BinaryImageGroupOperationT<MultiBandWeight> multiBandWeightedIntensity {normalizeWeightBands, alignIntensity}; // DEBUG
	//BinaryImageGroupOperationT<MultiBandWeight> multiBandWeighted {normalizeWeightBands, alignSource};
	//ImageGroupOperationT<FilterBank> intensityBands {alignIntensity}; // DEBUG

	//BinaryImageGroupOperationT<Multiply> multiBandWeightedIntensity {normalizeWeightBands, intensityBands}; // Applies
	//ImageGroupFoldT<Sum> blend {multiBandWeightedIntensity}; // Blends images

	// FIXME: need nested image groups
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

struct Transpose : OperatorT<Transpose> {
	string name() const override { return  "[transpose]"; }
};
/// Swaps component and group indices
struct TransposeOperation : GenericImageOperation, ImageGroupSource, Transpose {
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

	//ImageGroupFoldT<Prism> prismMaximumWeights {maximumWeights};
	sRGBOperation sRGBs [1] {/*{prismMaximumWeights},*/{blend}};

	/*TransposeOperation transpose[3] {normalizeWeightBands, intensityBands, multiBandWeightedIntensity};
	ImageGroupOperationT<Prism> prism[3] {{transpose[0]}, {transpose[1]}, {transpose[2]}};
	sRGBGroupOperation sRGBGroups [3] {{prism[0]}, {prism[1]}, {prism[2]}};*/

	ImageSourceView sRGBViews [1] {{sRGBs[0], &index}/*,{sRGBs[1], &index}*/};
	//ImageGroupSourceView sRGBGroupViews [3] {{sRGBGroups[0], &index}, {sRGBGroups[1], &index}, {sRGBGroups[2], &index}};

	//WidgetCycle view {{&sRGBViews[0], &sRGBGroupViews[0], &sRGBGroupViews[1], &sRGBGroupViews[2], &sRGBViews[1]}};*/
	WidgetCycle view {{&sRGBViews[0]}};
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
