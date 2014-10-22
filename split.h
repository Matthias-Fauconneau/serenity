#pragma once
#include "operation.h"
#include "source.h"
#include "process.h"

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

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	static constexpr float threshold = 0.04; // 0.18 with normalized bandpass
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
			SourceImage image = difference.image(endIndex, 0, difference.maximumSize()/4);
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
		//return array<size_t>(groups[groupIndex].slice(0, 2)); // DEBUG: Align debug
		return copy(groups[groupIndex]);
	}

	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};
