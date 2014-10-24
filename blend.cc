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

	/*ImageGroupOperationT<Exposure> exposure {alignSource};
	ImageGroupOperationT<LowPass> lowExposure {exposure};
	ImageGroupOperationT<SelectMaximum> selectExposure {lowExposure};
	ImageGroupOperationT<NormalizeSum> normalizeExposure {exposure};
	ImageGroupOperationT<SmoothStep> exposureStep {normalizeExposure};
	ImageGroupOperationT<NormalizeSum> normalizeStepExposure {exposureStep};

	ImageGroupOperationT<Contrast> contrast {alignSource};
	ImageGroupOperationT<LowPass> lowContrast {contrast};
	ImageGroupOperationT<SelectMaximum> selectContrast {lowContrast};
	ImageGroupOperationT<NormalizeSum> normalizeContrast {contrast};
	ImageGroupOperationT<SmoothStep> contrastStep {normalizeContrast};
	ImageGroupOperationT<NormalizeSum> normalizeStepContrast {contrastStep};

	ImageGroupOperationT<Saturation> saturation {alignSource};
	ImageGroupOperationT<LowPass> lowSaturation {saturation};
	ImageGroupOperationT<SelectMaximum> selectSaturation {lowSaturation};
	ImageGroupOperationT<NormalizeSum> normalizeSaturation {saturation};
	ImageGroupOperationT<SmoothStep> saturationStep {normalizeSaturation};
	ImageGroupOperationT<NormalizeSum> normalizeStepSaturation {saturationStep};

	ImageGroupOperationT<Weight> sum {alignSource};
	ImageGroupOperationT<LowPass> lowSum {sum};
	ImageGroupOperationT<SelectMaximum> selectSum {lowSum};*/

	ImageGroupOperationT<Exposure> weights {alignSource};
	ImageGroupOperationT<LowPass> lowWeights {weights};
	//ImageGroupOperationT<SelectMaximum> selectWeights {weights};
	ImageGroupOperationT<SmoothStep> step {weights};
	ImageGroupOperationT<NormalizeSum> selectWeights {step};

	BinaryImageGroupOperationT<Multiply> selected {selectWeights, alignSource};
	ImageGroupFoldT<Sum> select {selected};

	/*ImageGroupOperationT<NormalizeSum> normalizeSum {weights};
	ImageGroupOperationT<SmoothStep> step {normalizeSum};
	ImageGroupOperationT<NormalizeSum> normalizeStep {step};

	BinaryImageGroupOperationT<Multiply> weighted {normalizeStep, alignSource};
	ImageGroupFoldT<Sum> direct {weighted};*/

	ImageGroupOperationT<WeightFilterBank> weightBands {selectWeights}; // Splits each weight selection in bands
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {weightBands}; // Normalizes weight selection for each band

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

#if 0
	ImageGroupFoldT<Prism> prism [3] {
		//exposure, contrast, saturation, sum
		//lowExposure, lowContrast, lowSaturation, lowSum
		//normalizeExposure, normalizeContrast, normalizeSaturation, normalizeSum
		//selectExposure, selectSaturation, selectContrast, selectSum
		//normalizeStepExposure, normalizeStepContrast, normalizeStepSaturation,
		weights, lowWeights, selectWeights //normalizeSum, normalizeStep
	};
	array<sRGBOperation> sRGB = apply(mref<ImageGroupFoldT<Prism>>(prism), [&](ImageSource& source) -> sRGBOperation { return source; });
#else
	sRGBOperation sRGB [2] {select, blend};
#endif
	array<ImageSourceView> sRGBView = apply(mref<sRGBOperation>(sRGB),
											[&](ImageRGBSource& source) -> ImageSourceView { return {source, &index}; });
	/*TransposeOperation transposeWeightBands [1] {weightBands};
	ImageGroupOperationT<Prism> prism [1] {transposeWeightBands[0]};
	sRGBGroupOperation sRGBGroups [1] {prism[0]};
	array<ImageGroupSourceView> sRGBGroupView = apply(mref<sRGBGroupOperation>(sRGBGroups),
											[&](sRGBGroupOperation& source) -> ImageGroupSourceView { return {source, &index}; });*/
	array<ImageGroupSourceView> sRGBGroupView;
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
