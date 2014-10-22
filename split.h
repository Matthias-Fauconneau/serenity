#pragma once
#include "operation.h"
#include "source.h"
#include "process.h"

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	ImageSource& source;
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	map<size_t, real> SSEs;
	double SSE(size_t indexA, size_t indexB) {
		assert_(indexB == indexA+1, indexA, indexB);
		double& SSE = SSEs[indexA];
		if(SSE) return SSE;
		assert_(source.outputs() == 1);
		int2 size = source.maximumSize()/8;
		SourceImage A = source.image(indexA, 0, size);
		SourceImage B = source.image(indexB, 0, size);
		return SSE = parallel::SSE(A, B);
	}

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex;
		for(; endIndex+1 < source.count(); endIndex++) {
			float previous = endIndex > 0 ? SSE(endIndex-1, endIndex) : inf;
			float current = SSE(endIndex, endIndex+1);
			float next = endIndex+2 < source.count() ? SSE(endIndex+1, endIndex+2) : inf;
			if(endIndex == startIndex && startIndex>0) {
				if(previous/current < current/next) startIndex++; // Skips single unmatched image
			}
			else if((previous+next)/2 < current) break;
		}
		assert_(endIndex < source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append(index);
		log(group, apply(group, [&](const size_t index){ return source.elementName(index); }));
		assert_(group.size > 1);
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
