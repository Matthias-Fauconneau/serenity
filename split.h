#pragma once
#include "operation.h"
#include "source.h"
#include "process.h"

static double SSE(const ImageF& A, const ImageF& B, int2 offset) {
	assert_(A.size == B.size);
	int2 size = A.size - abs(offset);
	double energy = sumXY(size, [&A, &B, offset](int x, int y) {
		int2 p = int2(x,y);
		int2 a = p + max(int2(0), -offset);
		int2 b = p + max(int2(0),  offset);
		assert_(b-a==offset, offset, a, b);
		assert_(a >= int2(0) && a < A.size);
		assert_(b >= int2(0) && b < B.size);
		return sq(A(a) - B(b)); // SSE
	}, 0.0);
	energy /= size.x*size.y;
	return energy;
}

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	ImageSource& source;
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	map<size_t, map<size_t, real>> SSEs;
	real SSE(size_t indexA, size_t indexB) {
		real& SSE = SSEs[indexA][indexB];
		if(SSE) return SSE;
		assert_(source.outputs() == 1);
		int2 size = source.maximumSize()/8;
		SourceImage A = source.image(indexA, 0, size);
		SourceImage B = source.image(indexB, 0, size);
		float bestSSE = inf; int2 bestOffset = 0;
		const int delta = size.x / 12, steps = 2;
		for(int y: range(-steps, steps +1)) { int2 offset (0, y*delta/steps);
			float SSE = ::SSE(A, B, offset); // (size.x*size.y);
			if(SSE < bestSSE) { bestSSE = SSE, bestOffset = offset; }
		}
		if(bestOffset.y!=0) log(source.elementName(indexA), source.elementName(indexB), bestOffset.y, delta, bestSSE);
		SSE = bestSSE;
		assert_(SSE > 0 && SSE < 4);
		return SSE;
	}

	bool nextGroup() {
		size_t start = groups ? groups.last().last()+1 : 0;
		if(start == source.count()) return false;
		real worstMatch =  SSE(start, start+1); // Splits when best match is worse than worst match in collection
		size_t candidate = start+1;
		while(candidate < start+4) {
			candidate++; // Next candidate
			if(candidate+1 >= source.count()) { candidate=source.count(); break; } // All items already split (Assumes last item is not single)
			real bestMatch=inf; for(size_t index: range(start, candidate)) bestMatch=min(bestMatch, SSE(index, candidate)); // Match previous
			real next = SSE(candidate, candidate+1); // Match next
			log(worstMatch, bestMatch, next);
			if(bestMatch > next*2/3 && (bestMatch > next*3/2 || bestMatch > worstMatch)) break;
			worstMatch = max(worstMatch, bestMatch);
		}
		assert_(candidate <= source.count());
		array<size_t> group;
		for(size_t index: range(start, candidate)) group.append(index);
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
