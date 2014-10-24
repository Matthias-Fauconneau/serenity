/// \file stitch.cc PanoramaStitch
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "split.h"
#include "operation.h"
#include "align.h"
#include "weight.h"
#include "multiscale.h"
#include "jpeg-encoder.h"

#if 1
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
#else
/// Returns consecutive pairs
struct ConsecutivePairs : virtual GroupSource {
	Source& source;
	ConsecutivePairs(Source& source) : source(source) {}
	size_t count(size_t unused need=0) override { return source.count()-1; }
	int64 time(size_t index) override { return max(apply(operator()(index), [this](size_t index) { return source.time(index); })); }
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count());
		array<size_t> pair;
		pair.append(groupIndex);
		pair.append(groupIndex+1);
		return pair;
	}
};
#endif

struct Mean : ImageGroupOperator1, OperatorT<Mean> {
	void apply(const ImageF& Y, ref<ImageF> X) const override {
		parallel::apply(Y, [&](size_t index) { return sum<float>(::apply(X, [index](const ImageF& x) { return x[index]; })) / X.size; });
	}
};

struct PanoramaStitch {
	Folder folder {"Pictures/Panorama", home()};

	ImageFolder source { folder };
	ImageOperationT<Intensity> intensity {source};
	ImageOperationT<Normalize> normalize {intensity};
	AllImages groups {source};
	//ConsecutivePairs groups {source};

	GroupImageOperation groupNormalize {normalize, groups};
	ImageGroupTransformOperationT<Align> transforms {groupNormalize};

	GroupImageOperation groupSource {source, groups};
	SampleImageGroupOperation alignSource {groupSource, transforms};
	ImageGroupFoldT<Sum> align {alignSource};
};

struct PanoramaStitchPreview : PanoramaStitch, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	sRGBOperation sRGB = align;
	ImageSourceView view = {sRGB, &index};
	Window window {&view, -1, [this]{ return view.title(); }};
};
registerApplication(PanoramaStitchPreview);
