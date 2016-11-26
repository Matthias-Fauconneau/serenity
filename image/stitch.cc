/// \file stitch.cc PanoramaStitch
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "prism.h"
#include "multiscale.h"
#include "layout.h"
#include "jpeg.h"

generic size_t reversiblePartition(mref<T> at, size_t left, size_t right, size_t pivotIndex, mref<size_t> indices) {
	swap(at[pivotIndex], at[right]); swap(indices[pivotIndex], indices[right]);
	const T& pivot = at[right];
	size_t storeIndex = left;
	for(size_t i: range(left,right)) {
		if(at[i] > pivot) {
			swap(at[i], at[storeIndex]); swap(indices[i], indices[storeIndex]);
			storeIndex++;
		}
	}
	swap(at[storeIndex], at[right]); swap(indices[storeIndex], indices[right]);
	return storeIndex;
}
generic void reversibleQuickSort(mref<T> at, int left, int right, mref<size_t> indices) {
	if(left < right) { // If the list has 2 or more items
		int pivotIndex = reversiblePartition(at, left, right, (left + right)/2, indices);
		if(pivotIndex) reversibleQuickSort(at, left, pivotIndex-1, indices);
		reversibleQuickSort(at, pivotIndex+1, right, indices);
	}
}
generic buffer<size_t> reversibleSort(mref<T> at) {
	buffer<size_t> indices (at.size);
	for(size_t index: range(indices.size)) indices[index]=index;
	reversibleQuickSort(at, 0, at.size-1, indices);
	buffer<size_t> reverse (indices.size);
	for(size_t index: range(indices.size)) reverse[indices[index]] = index;
	return reverse;
}

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
	PanoramaWeights(ImageGroupSource& source, TransformGroupSource& transform) : source(source), transform(transform) {}

	size_t count(size_t need=0) override { return source.count(need); }
	int64 time(size_t groupIndex) override { return max(source.time(groupIndex), transform.time(groupIndex)); }
	String name() const override { return source.name()+" PanoramaWeights"; }
	String path() const override { return source.path()+"/PanoramaWeights"; }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override { return source.elementName(groupIndex); }
	int2 size(size_t groupIndex, int2 hint) const override {
		int2 sourceSize = this->sourceSize(groupIndex, hint);
		int2 min,max; minmax(transform(groupIndex, sourceSize), min, max);
		return max-min;
	}
	size_t outputs() const override { return 1; }
    size_t groupSize(size_t groupIndex) const override { return source.groupSize(groupIndex); }

	int2 sourceHint(size_t groupIndex, int2 hint) const {
		if(!hint) return source.size(groupIndex, 0);
		int2 fullTargetSize = this->size(groupIndex, 0);
		int2 fullSourceSize = source.size(groupIndex, 0);
		return hint.y*fullSourceSize.y/fullTargetSize.y;
	}
	int2 sourceSize(size_t groupIndex, int2 hint) const { return source.size(groupIndex, sourceHint(groupIndex, hint)); }

	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 hint, string) override {
		assert_(componentIndex == 0);
		int2 sourceSize = this->sourceSize(groupIndex, hint);
		auto transforms = transform(groupIndex, sourceSize);
		int2 min,max; minmax(transforms, min, max);
		int2 size = max-min;
		auto reverse = reversibleSort(transforms); // by X offset
		array<SourceImage> images = apply(transforms.size, [&](size_t index) -> SourceImage {
            SourceImage image (size); //, align(8, size.x));
			auto current = transforms[index];
			int currentMin = current.min(sourceSize).x  - min.x;
			int previousMax = (index == 0) ? currentMin : (transforms[index-1].max(sourceSize).x - min.x);
			int currentMax = current.max(sourceSize).x - min.x;
			int nextMin = (index == transforms.size-1) ? currentMax : (transforms[index+1].min(sourceSize).x - min.x);
			if(previousMax > nextMin) previousMax=nextMin= (previousMax+nextMin)/2;
			assert_(currentMin <= previousMax && previousMax <= nextMin && nextMin <= currentMax,
					currentMin, previousMax, nextMin, currentMax);
			for(size_t y : range(image.size.y)) {
				for(size_t x : range(currentMin)) image(x,y) = 0;
				for(size_t x : range(currentMin, (currentMin+previousMax)/2)) image(x,y) = 0;
				for(size_t x : range((currentMin+previousMax)/2, previousMax)) image(x,y) = 1;
				for(size_t x : range(previousMax, nextMin)) image(x,y) = 1;
				for(size_t x : range(nextMin, (nextMin+currentMax)/2)) image(x,y) = 1;
				for(size_t x : range((nextMin+currentMax)/2, currentMax)) image(x,y) = 0;
				for(size_t x : range(currentMax, image.size.x)) image(x,y) = 0;
			}
			return image;
        } );
        return apply(reverse, [&](size_t index) { return move(images[index]); });
	}
};

struct PanoramaStitch {
	const Folder& folder = currentWorkingDirectory();
    ImageFolder source {folder, {}, 2 /*Downsample*/};
	AllImages groups {source};

	ImageOperationT<Intensity> intensity {source};
    GroupImageOperation groupIntensity {intensity, groups};
	ImageGroupTransformOperationT<Align> transforms {groupIntensity};

	GroupImageOperation groupSource {source, groups};
	SampleImageGroupOperation alignSource {groupSource, transforms};

	PanoramaWeights weights {groupSource, transforms};
	ImageGroupOperationT<NormalizeSum> normalizeWeights {weights};
	BinaryImageGroupOperationT<Multiply> applySelectionWeights {normalizeWeights, alignSource};
	ImageGroupFoldT<Sum> select {applySelectionWeights};

	ImageGroupOperationT<WeightFilterBank> weightBands {weights}; // Splits each weight selection in bands
	ImageGroupOperationT<NormalizeSum> normalizeWeightBands {weightBands}; // Normalizes weight selection over images for each band

    ImageGroupForwardComponent multiscale {alignSource, sumBands};
    ImageGroupOperationT<FilterBank> splitBands {multiscale.source};
	BinaryImageGroupOperationT<Multiply> weightedBands {normalizeWeightBands, splitBands}; // Applies weights to each band
	ImageGroupOperationT<Sum> sumBands {weightedBands}; // Sums bands
    ImageGroupFoldT<Sum> blend {multiscale}; // Sums images
};

struct PanoramaStitchPreview : PanoramaStitch, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;
	size_t imageIndex = 0;

#if 0
    sRGBGroupOperation sRGB[3] = {alignSource, normalizeWeightBands, multiscale};
#if 0
    array<Scroll<ImageGroupSourceView>> sRGBView = apply(mref<sRGBGroupOperation>(sRGB), [&](ImageRGBGroupSource& source) {
            return Scroll<ImageGroupSourceView>(source, &index, &imageIndex); });
#else
    array<ImageGroupSourceView> sRGBView = apply(mref<sRGBGroupOperation>(sRGB), [&](ImageRGBGroupSource& source) {
      return ImageGroupSourceView(source, &index, &imageIndex); });
#endif
#else
	sRGBOperation sRGB [1] = {blend};
    //array<Scroll<ImageSourceView>> sRGBView = apply(mref<sRGBOperation>(sRGB), [&](ImageRGBSource& source) -> Scroll<ImageSourceView> { return {source, &index}; });
    array<ImageSourceView> sRGBView = apply(mref<sRGBOperation>(sRGB), [&](ImageRGBSource& source) -> ImageSourceView { return {source, &index}; });
#endif
	VBox views {toWidgets(sRGBView), VBox::Share, VBox::Expand};
    unique<Window> window = ::window(&views);
};
registerApplication(PanoramaStitchPreview, preview);

struct PanoramaStitchExport : PanoramaStitch, Application {
	sRGBOperation sRGB {blend};
	PanoramaStitchExport() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time time;
			SourceImageRGB image = sRGB.image(index, int2(0,0));
			time.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),'\t',sRGB.elementName(index), strx(image.size),'\t',time);
		}
	}
};
registerApplication(PanoramaStitchExport);
