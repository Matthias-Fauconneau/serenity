#pragma once
#include "source.h"

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	ImageSource& source;
	map<size_t, map<size_t, real>> SSEs;
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	real SSE(size_t indexA, size_t indexB);
	bool nextGroup();

	size_t count(size_t need) override { while(groups.size < need && nextGroup()) {} return groups.size; }
	array<size_t> operator()(size_t groupIndex) override {
		while(groups.size <= groupIndex) assert_( nextGroup() );
		return array<size_t>(groups[groupIndex].slice(0, 2)); // Assumes first two images are the best brackets
		return copy(groups[groupIndex]);
	}
	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }
};
