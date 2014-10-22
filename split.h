#pragma once
#include "operation.h"
#include "source.h"
#include "process.h"

/// Evaluates residual energy between A and transform B
static double residualEnergy(const ImageF& A, const ImageF& B, int2 offset) {
	assert_(A.size == B.size);
	int2 size = A.size - abs(offset);
	double energy = sumXY(size, [&A, &B, offset](int x, int y) {
		int2 p = int2(x,y);
		int2 a = p + max(int2(0), -offset);
		int2 b = p + max(int2(0),  offset);
		assert_(b-a==offset, offset, a, b);
		assert_(a >= int2(0) && a < A.size);
		assert_(b >= int2(0) && b < B.size);
		//return A(a) * B(b); // Cross correlation
		return sq(A(a) - B(b)); // SSE
	}, 0.0);
	energy /= size.x*size.y;
	//energy -= size.x*size.y; // Advantages more overlap
	//log(A.size, offset, energy);
	return energy;
}

/// Splits sequence in groups separated when difference between consecutive images is greater than a threshold
struct DifferenceSplit : GroupSource {
	ImageSource& source;
	array<array<size_t>> groups;

	DifferenceSplit(ImageSource& source) : source(source) {}

	map<size_t, real> residuals;
	double residual(size_t indexA, size_t indexB) {
		assert_(indexB == indexA+1, indexA, indexB);
		assert_(source.outputs() == 1);
		int2 size = source.maximumSize()/8;
		SourceImage A = source.image(indexA, 0, size);
		SourceImage B = source.image(indexB, 0, size);

		double& bestResidual = residuals[indexA];
		if(bestResidual) return bestResidual;
		/// Best match of 13 offsets
		bestResidual = inf; int2 bestOffset=0;
		//for(;;) {
			int2 stepOffset = bestOffset;
			const int delta = size.y/2;
			for(int y: range(-delta, delta +1)) {
				int2 offset = int2(0, y);
				double residual = residualEnergy(A, B, offset/**size/16*/);
				residual += residual*abs(offset.y)/size.y; // Penalizes large offsets
				if(residual < bestResidual) {
					bestResidual = residual;
					stepOffset = offset;
				}
			}
			//if(stepOffset == bestOffset) { bestOffset=stepOffset; break; }
			bestOffset = stepOffset;
		//}
		log(indexA, indexB, bestOffset.y, delta);
		return bestResidual;
	}

	bool nextGroup() {
		size_t startIndex = groups ? groups.last().last()+1 : 0;
		if(startIndex == source.count()) return false;
		size_t endIndex = startIndex;
		for(; endIndex+1 < source.count(); endIndex++) {
			float previous = endIndex > 0 ? residual(endIndex-1, endIndex) : inf;
			float current = residual(endIndex, endIndex+1);
			float next = endIndex+2 < source.count() ? residual(endIndex+1, endIndex+2) : inf;
			log(str(previous,3), source.elementName(endIndex), str(current,3), source.elementName(endIndex+1), str(next,3),
				previous/**(1+1./128)*/ < current/*, current > next*(1+1./128)*/, 1/(previous/current-1));
			if(endIndex == startIndex) {
				if(previous*(1-1./128) < current) {
					log("SKIP", source.elementName(endIndex), previous, current);
					startIndex++; // Single
				}
			}
			else if((previous+next)/2 < current) break;
#if 0
			if(endIndex > startIndex) { if(previous < current /*|| current > next*/) break; } // Breaks if bigger error than previous match
			else if(previous <= current /*|| current > next*/) break; // Breaks if bigger error than previous break
			assert_(previous != current);
#endif
		}
		assert_(endIndex < source.count(), endIndex, source.count());
		array<size_t> group;
		for(size_t index: range(startIndex, endIndex+1/*included*/)) group.append( index );
		log(group, apply(group, [&](const size_t index){ return source.elementName(index); }));
		assert_(group.size > 1, group.size);
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
