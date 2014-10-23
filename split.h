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
	real SSE(size_t indexA, size_t indexB) {
		assert_(indexB == indexA+1, indexA, indexB);
		real& SSE = SSEs[indexA];
		if(SSE) return SSE;
		assert_(source.outputs() == 1);
		int2 size = source.maximumSize()/8;
		SourceImage A = source.image(indexA, 0, size);
		SourceImage B = source.image(indexB, 0, size);
		SSE = parallel::SSE(A, B) / (size.x*size.y);
		assert_(SSE > 0 && SSE < 4);
		return SSE;
	}

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex;
		for(; endIndex+1 < source.count(); endIndex++) {
			real previous = endIndex > 0 ? SSE(endIndex-1, endIndex) : inf;
			real current = SSE(endIndex, endIndex+1);
			real next = endIndex+2 < source.count() ? SSE(endIndex+1, endIndex+2) : inf;
			log(previous, source.elementName(endIndex), current, source.elementName(endIndex+1), next);
			if(endIndex == startIndex && startIndex>0) {
				//if(current > 2./3) {
				//if((previous+next)/2 < current) {
				/*if(next < current) {
					log("SKIP",  source.elementName(endIndex));
					startIndex++; // Skips single unmatched image
				}*/
			}
			//else if((previous+next)/2 < current) break;
			else if(previous < current) break;
		}
		assert_(endIndex < source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append(index);
		assert_(group.size > 1);
		groups.append(move(group));
		return true;
	}

	size_t count(size_t need) override { while(groups.size < need && nextGroup()) {} return groups.size; }

	/// Returns image indices for group index
	array<size_t> operator()(size_t groupIndex) override {
		assert_(groupIndex < count(groupIndex+1), groupIndex, groups.size);
		//return array<size_t>(groups[groupIndex].slice(0, 2)); // Assumes first two images are the best brackets
		return copy(groups[groupIndex]);
	}

	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};
