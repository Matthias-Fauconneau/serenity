/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "process.h"
#include "normalize.h"
#include "difference.h"
#include "source-view.h"

/// Returns groups of consecutive images pairs
struct ConsecutivePairs : GroupSource {
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

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : ImageGroupSource {
	static constexpr float threshold = 0.41;
	ConsecutivePairs pairs {source};
	Subtract subtract;
	ProcessedGroupImageSource difference{source, pairs, subtract};
	array<array<size_t>> groups;

	using ImageGroupSource::ImageGroupSource;

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex+1; // Included
		for(; endIndex < difference.count(); endIndex++) {
			SourceImage image = difference.image(endIndex, 0, int2(1024,768));
			float meanEnergy = parallel::energy(image)/image.ref::size;
			if(meanEnergy > threshold) break;
		}
		assert_(endIndex < source.count(), endIndex, source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append( index );
		groups.append(move(group));
		return true;
	}

	size_t count(size_t need) override { while(groups.size < need && nextGroup()) {} return groups.size; }

	/// Returns image indices for group index
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count(groupIndex), groupIndex, groups.size);
		return copy(groups[groupIndex]);
	}
};

struct Mean : ImageGroupOperation1, OperationT<Mean> {
	virtual int outputs() const { return 1; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
	}
};

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Normalize> normalize {source};
	DifferenceSplit split {normalize};
	Mean mean;
	ProcessedGroupImageSource processed {source, split, mean};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	/*PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;*/
	size_t index = 0;

	ImageSourceView sourceView {source, &index, window};
	ImageSourceView processedView {processed, &index, window};
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
