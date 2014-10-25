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

struct Binary : ImageOperator1, OperatorT<Binary> {
	void apply(const ImageF& Y, const ImageF& X) const override { ::apply(Y, [&](float x) { return x ? 1 : 0; }, X); }
};

struct Mask : ImageOperator, OperatorT<Mask> {
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(X.size==2 && Y.size==1);
		::apply(Y[0], [&](float x0, float x1) { return x0 ? x1 : 0; }, X[0], X[1]);
	}
};

struct PanoramaWeights : ImageGroupSource {
	ImageGroupSource& source;
	TransformGroupSource& transform;
	Folder cacheFolder {"[sample]", source.folder(), true};
	PanoramaWeights(ImageGroupSource& source, TransformGroupSource& transform) : source(source), transform(transform) {}

	size_t count(size_t need=0) override { return source.count(need); }
	int64 time(size_t groupIndex) override { return max(source.time(groupIndex), transform.time(groupIndex)); }
	String name() const override { return str(source.name(), "[sample]"); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override { return source.elementName(groupIndex); }
	int2 size(size_t groupIndex, int2 hint) const override {
		int2 sourceSize = this->sourceSize(groupIndex, hint);
		int2 min,max; minmax(transform(groupIndex, sourceSize), source.size(groupIndex, sourceSize), min, max);
		return max-min;
	}
	size_t outputs() const override { return 1; }
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }

	int2 sourceHint(size_t groupIndex, int2 hint) const {
		if(!hint) return source.size(groupIndex, 0);
		// Scales source size
		int2 fullTargetSize = this->size(groupIndex, 0);
		int2 fullSourceSize = source.size(groupIndex, 0);
		return hint*fullSourceSize/fullTargetSize;
	}
	int2 sourceSize(size_t groupIndex, int2 hint) const { return source.size(groupIndex, sourceHint(groupIndex, hint)); }

	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 hint=0, bool unused noCacheWrite = false) override {
		assert_(componentIndex == 0);
		int2 sourceSize = this->sourceSize(groupIndex, hint);
		auto transforms = transform(groupIndex, sourceSize);
		int2 min,max; minmax(transforms, sourceSize, min, max);
		int2 size = max-min;

		sort(transforms); // by X offset
		return apply(transforms.size, [&](size_t index) -> SourceImage {
			SourceImage image (size);
			auto current = transforms[index];
			int start, end;
			if(index == 0) start =0;
			else {
				auto previous = transforms[index-1];
				int currentMin = current.min(sourceSize).x;
				int previousMax = previous.max(sourceSize).x;
				assert_(currentMin < previousMax);
				int midX = (currentMin + previousMax)/2 - min.x;
				assert_(midX > 0 && midX < size.x, midX);
				start = midX;
			}
			if(index==transforms.size-1) end = image.size.x;
			else {
				auto next = transforms[index+1];
				int nextMin = next.min(sourceSize).x;
				int currentMax = current.max(sourceSize).x;
				assert_(nextMin < currentMax);
				int midX = (nextMin + currentMax)/2 - min.x;
				assert_(midX > 0 && midX < size.x, midX);
				end = midX;
			}
			for(size_t y : range(image.size.y)) { //FIXME: parallel + SIMD
				for(size_t x : range(start)) image(x,y) = 0;
				for(size_t x : range(start, end)) image(x,y) = 1;
				for(size_t x : range(end, image.size.x)) image(x,y) = 0;
			}
			return image;
		} );
	}
};

/// Forwards a component transparently across a single component group operation
struct ImageGroupForwardComponent : ImageGroupSource {
	ImageGroupSource& input;
	ImageGroupSource& target;
	struct ImageGroupForwardComponentSource : ImageGroupSource {
		ImageGroupForwardComponent& forward;
		ImageGroupForwardComponentSource(ImageGroupForwardComponent& forward) : forward(forward) {}
		size_t count(size_t need=0) { return forward.input.count(need); }
		int64 time(size_t index) { return forward.input.time(index); }
		String name() const { return forward.input.name(); }
		const Folder& folder() const { return forward.input.folder(); }
		int2 maximumSize() const { return forward.input.maximumSize(); }
		String elementName(size_t index) const { return forward.input.elementName(index); }
		int2 size(size_t index, int2 size=0) const { return forward.input.size(index, size); }
		size_t groupSize(size_t groupIndex) const { return forward.input.groupSize(groupIndex); }
		size_t outputs() const { return 1; }
		array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite = false) {
			assert_(componentIndex == 0);
			return forward.input.images(groupIndex, forward.componentIndex, size, noCacheWrite);
		}
	} source {*this};
	int componentIndex = -1; // FIXME: upgrade ImageSource interface to pass private parameters
	ImageGroupForwardComponent(ImageGroupSource& input, ImageGroupSource& target) : input(input), target(target) {}

	size_t count(size_t need=0) { return target.count(need); }
	int64 time(size_t index) { return target.time(index); }
	String name() const { return target.name(); }
	const Folder& folder() const { return target.folder(); }
	int2 maximumSize() const { return target.maximumSize(); }
	String elementName(size_t index) const { return target.elementName(index); }
	int2 size(size_t index, int2 size=0) const { return target.size(index, size); }
	size_t groupSize(size_t groupIndex) const { return target.groupSize(groupIndex); }
	size_t outputs() const { return input.outputs(); }
	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite = false) override {
		this->componentIndex = componentIndex; // Forwards componentIndex through a private field
		assert_(target.outputs()==1);
		return target.images(groupIndex, 0, size, noCacheWrite);
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

	GroupImageOperation groupIntensity {intensity, groups};
	PanoramaWeights weights {groupIntensity, transforms};
	BinaryImageGroupOperationT<Multiply> applySelectionWeights {weights, alignSource};
	ImageGroupFoldT<Sum> select {applySelectionWeights};

	ImageGroupOperationT<WeightFilterBank> weightBands {weights}; // Splits each weight selection in bands
	BinaryImageGroupOperationT<Mask> maskWeightBands {alignIntensity, weightBands};
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {maskWeightBands}; // Normalizes weight selection for each band

	ImageGroupForwardComponent multiscale {alignSource, sumBands};
	ImageGroupOperationT<FilterBank> splitBands {multiscale.source};
	BinaryImageGroupOperationT<Multiply> weightedBands {normalizeWeightBands, splitBands}; // Applies weights to each band
	ImageGroupOperationT<Sum> sumBands {weightedBands}; // Sums bands
	ImageGroupOperationT<Sum> sumImages {multiscale}; // Sums images
};

struct PanoramaStitchPreview : PanoramaStitch, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageGroupFoldT<Prism> prism [1] {/*overlap, low, mask,*/ weights};
	//ImageGroupFoldT<Prism> prism [4] {exposure, lowExposure, maskLowExposure, weights};
	array<sRGBOperation> sRGB = apply(mref<ImageGroupFoldT<Prism>>(prism),
									  [&](ImageSource& source) -> sRGBOperation { return source; });
	array<Scroll<ImageSourceView>> sRGBView =
			apply(mref<sRGBOperation>(sRGB), [&](ImageRGBSource& source) -> Scroll<ImageSourceView> { return {source, &index}; });

	/*sRGBOperation sRGB2 [1] = {select};
	array<Scroll<ImageSourceView>> sRGBView2 =
			apply(mref<sRGBOperation>(sRGB2), [&](ImageRGBSource& source) -> Scroll<ImageSourceView> { return {source, &index}; });*/
	//VBox views {toWidgets(sRGBView)+toWidgets(sRGBView2), VBox::Share, VBox::Expand};
	VBox views {toWidgets(sRGBView), VBox::Share, VBox::Expand};
	Window window {&views, int2(0, 256), [this]{ return views.title(); }};
};
registerApplication(PanoramaStitchPreview);
