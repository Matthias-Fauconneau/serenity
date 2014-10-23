#include "split.h"

real DifferenceSplit::SSE(size_t indexA, size_t indexB) {
	real& SSE = SSEs[indexA][indexB];
	if(SSE) return SSE;
	assert_(source.outputs() == 1);
	int2 size = source.maximumSize()/16;
	SourceImage A = source.image(indexA, 0, size);
	SourceImage B = source.image(indexB, 0, size);
	float bestSSE = inf; int2 bestOffset = 0;
	const int delta = size.x / 12, steps = 2;
	for(int y: range(-steps, steps +1)) { int2 offset (0, y*delta/steps);
		float SSE = ::SSE(A, B, offset); // (size.x*size.y);
		if(SSE < bestSSE) { bestSSE = SSE, bestOffset = offset; }
	}
	//if(bestOffset.y!=0) log(source.elementName(indexA), source.elementName(indexB), bestOffset.y, delta, bestSSE);
	SSE = bestSSE;
	assert_(SSE > 0 && SSE < 4);
	return SSE;
}

bool DifferenceSplit::nextGroup() {
	size_t start = groups ? groups.last().last()+1 : 0;
	if(start == source.count()) return false;
	real worstMatch =  SSE(start, start+1); // Splits when best match is worse than worst match in collection
	size_t candidate = start+1;
	while(candidate < start+4) {
		candidate++; // Next candidate
		if(candidate+1 >= source.count()) { candidate=source.count(); break; } // All items already split (Assumes last item is not single)
		real bestMatch=inf; for(size_t index: range(start, candidate)) bestMatch=min(bestMatch, SSE(index, candidate)); // Match previous
		real next = SSE(candidate, candidate+1); // Match next
		//log(worstMatch, bestMatch, next);
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
