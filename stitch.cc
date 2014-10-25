/// \file stitch.cc PanoramaStitch
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "prism.h"
#include "multiscale.h"
#include "layout.h"
#include "jpeg-encoder.h"

struct AllImages : GroupSource {
	ImageSource& source;
	AllImages(ImageSource& source) : source(source) {}
	size_t count(size_t) override { return 1; }
	array<size_t> operator()(size_t) override {
		array<size_t> indices;
		for(size_t index: range(source.count())) indices.append( index );
		return indices;
	}
	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};

struct Mask : ImageOperator, OperatorT<Mask> {
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(X.size==2 && Y.size==1);
		::apply(Y[0], [&](float x0, float x1) { return x0 ? x1 : 0; }, X[0], X[1]);
	}
};

struct PanoramaStitch {
	Folder folder {"Pictures/Panorama", home()};

	ImageFolder source { folder };
	ImageOperationT<Intensity> intensity {source};
	ImageOperationT<Normalize> normalize {intensity};
	AllImages groups {source};

	GroupImageOperation groupNormalize {normalize, groups};
	ImageGroupTransformOperationT<Align> transforms {groupNormalize};

	GroupImageOperation groupSource {source, groups};
	SampleImageGroupOperation alignSource {groupSource, transforms};
	ImageGroupOperationT<Intensity> alignIntensity {alignSource};

	ImageGroupFoldT<Sum> sum {alignSource};

	ImageGroupOperationT<Exposure> exposure {alignSource};
	ImageGroupOperationT<LowPass> lowExposure {exposure};
	ImageGroupOperationT<NormalizeSum> normalizeExposure {lowExposure};
	ImageGroupOperationT<LowPass> lowNormalizeExposure {normalizeExposure};
	BinaryImageGroupOperationT<Mask> maskLowExposure {alignIntensity, lowNormalizeExposure};
	ImageGroupOperationT<NormalizeSum> weights {maskLowExposure};
	BinaryImageGroupOperationT<Multiply> applySelectionWeights {weights, alignSource};
	ImageGroupFoldT<Sum> select {applySelectionWeights};


	ImageGroupOperationT<WeightFilterBank> weightBands {weights}; // Splits each weight selection in bands
	BinaryImageGroupOperationT<Mask> maskWeightBands {alignIntensity, weightBands};
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {maskWeightBands}; // Normalizes weight selection for each band

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

struct PanoramaStitchPreview : PanoramaStitch, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	/*ImageGroupFoldT<Prism> prism [6] {exposure, lowExposure, normalizeExposure, lowNormalizeExposure, maskLowExposure, weights};
	array<sRGBOperation> sRGB = apply(mref<ImageGroupFoldT<Prism>>(prism),
									  [&](ImageSource& source) -> sRGBOperation { return source; });
	array<Scroll<ImageSourceView>> sRGBView =
			apply(mref<sRGBOperation>(sRGB), [&](ImageRGBSource& source) -> Scroll<ImageSourceView> { return {source, &index}; });*/

	sRGBOperation sRGB2 [2] = {select, blend};
	array<Scroll<ImageSourceView>> sRGBView2 =
			apply(mref<sRGBOperation>(sRGB2), [&](ImageRGBSource& source) -> Scroll<ImageSourceView> { return {source, &index}; });
	VBox views {/*toWidgets(sRGBView)+*/toWidgets(sRGBView2), VBox::Share, VBox::Expand};
	Window window {&views, int2(0,-1), [this]{ return views.title(); }};
};
registerApplication(PanoramaStitchPreview);
