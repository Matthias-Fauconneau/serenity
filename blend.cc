/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "source-view.h"

/// Returns groups of consecutive pairs
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

/// Returns images from an image source grouped by a group source
struct ProcessedImageGroupSource : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	ProcessedImageGroupSource(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
	size_t count(size_t need=0) override { return groups.count(need); }
	String name() const override { return source.name(); }
	int outputs() const override { return source.outputs(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t groupIndex) override { return max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })); }
	virtual String elementName(size_t groupIndex) const override {
		return str(apply(groups(groupIndex), [this](const size_t imageIndex) { return source.elementName(imageIndex); }));
	}
	int2 size(size_t groupIndex) const override {
		auto sizes = apply(groups(groupIndex), [this](size_t imageIndex) { return source.size(imageIndex); });
		for(auto size: sizes) assert_(size == sizes[0]);
		return sizes[0];
	}

	virtual array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) {
		return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, outputIndex, size, noCacheWrite); });
	}
};

/*generic struct ImageGroupSourceT : T, ImageGroupSource {
	ImageGroupSourceT(ImageSource& source) : T(source), ImageGroupSource(source) {}
	virtual int64 time(size_t groupIndex) override { return max(T::time(groupIndex), ImageGroupSource::time(groupIndex)); }
};*/

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	static constexpr float threshold = 0.18;
	ImageSource& source;
	ConsecutivePairs pairs {source};
	ProcessedImageGroupSource sourcePairs {source, pairs};
	Subtract subtract;
	ProcessedGroupImageSource difference{sourcePairs, subtract};
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex+1; // Included
		for(; endIndex < difference.count(); endIndex++) {
			SourceImage image = difference.image(endIndex, 0, difference.maximumSize()/16);
			float meanEnergy = parallel::energy(image)/image.ref::size;
			log(endIndex, difference.elementName(endIndex), meanEnergy);
			if(meanEnergy > threshold) break;
		}
		assert_(endIndex < source.count(), endIndex, source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append( index );
		log(group);
		groups.append(move(group));
		return true;
	}

	size_t count(size_t need) override { while(groups.size < need && nextGroup()) {} return groups.size; }

	/// Returns image indices for group index
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count(groupIndex+1), groupIndex, groups.size);
		return copy(groups[groupIndex]);
	}

	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};

struct Mean : ImageGroupOperation1, OperationT<Mean> {
	virtual int outputs() const { return 1; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
	}
};

struct Transform {
	vec2 operator()(vec2);
};

struct TransformGroupSource {
	virtual array<Transform> operator()(size_t groupIndex);
};

/// Evaluates transforms for a group of images
struct ImageTransformGroupOperation : Operation {
	virtual array<Transform> operator()(ref<ImageF>) const = 0;
};

struct ProcessedImageTransformGroupSource : TransformGroupSource {
	ImageGroupSource& source;
	ImageTransformGroupOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedImageTransformGroupSource(ImageGroupSource& source, ImageTransformGroupOperation& operation) :
		source(source), operation(operation) {}

	array<Transform> operator()(size_t groupIndex) override {
		array<SourceImage> images = source.images(groupIndex, 0);
		return operation(apply(images, [](const SourceImage& x){ return share(x); }));
	}
};

#if 0
// ProcessedGroupImageGroupSource

/// Processes every image in a group individually
struct ProcessedGroupImageGroupSource : ImageGroupSource {
	ImageGroupSource& source;
	ImageOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedGroupImageGroupSource(ImageGroupSource& source, ImageOperation& operation) : source(source), operation(operation) {}

#if 0
	int outputs() const /*override*/ { return operation.outputs(); }
	const Folder& folder() const /*override*/ { return cacheFolder; }
	String name() const /*override*/ { return str(operation.name(), source.name()); }
	size_t count(size_t need=0) /*override*/ { return groups.count(need); }
	int2 maximumSize() const /*override*/ { return source.maximumSize(); }
	String elementName(size_t groupIndex) const /*override*/ {
		return str(apply(groups(groupIndex), [this](const size_t index) { return source.elementName(index); }));
	}
	int64 time(size_t groupIndex) override {
		return max(operation.time(), max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })));
	}
	int2 size(size_t groupIndex) const /*override*/ {
		auto sizes = apply(groups(groupIndex), [this](size_t index) { return source.size(index); });
		for(auto size: sizes) assert_(size == sizes[0]);
		return sizes[0];
	}
#endif

	array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) override;
};
#endif

/// Evaluates transforms to align groups of images

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Normalize> normalize {source};
	DifferenceSplit split {normalize};
	ProcessedImageGroupSource groups {source, split};

	/*Align align; : ImageTransformGroupOperation
	ProcessedImageTransformGroupSource transforms {groups, align};*/

	//Transform transform {groups, transforms} : ImageGroupSource

	Mean mean;
	ProcessedGroupImageSource processed {groups/*transformed*/, mean};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	/*PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;*/
	size_t index = 0;

	ImageSourceView sourceView {source, &index, window};
	ImageSourceView processedView {processed, &index, window, source.maximumSize()/16};
	WidgetToggle toggleView {&sourceView, &processedView, 1};
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
