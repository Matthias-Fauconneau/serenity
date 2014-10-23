#include "weight.h"

void Contrast::apply(const ImageF& Y, const ImageF& X) const {
	forXY(Y.size, [&](uint x, uint y) {
		int2 A = int2(x,y);
		float sum = 0, SAD = 0;
		float a = X(A);
		for(int dy: range(3)) for(int dx: range(3)) {
			int2 B = A+int2(dx-1, dy-1);
			if(!(B >= int2(0) && B < X.size)) continue;
			float b = X(B);
			sum += b;
			SAD += abs(a - b);
		}
		Y(A) = sum ? SAD : 0;
	});
}

void MaximumWeight::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(Y.size == X.size);
	forXY(Y[0].size, [&](uint x, uint y) {
		int best = -1; float max = 0;
		for(size_t index: range(X.size)) { float v = X[index](x, y); if(v > max) { max = v, best = index; } }
		for(size_t index: range(Y.size)) Y[index](x, y) = 0;
		if(best>=0) Y[best](x, y) = 1;
	});
}

void NormalizeWeights::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(Y.size == X.size);
	forXY(Y[0].size, [&](uint x, uint y) {
		float sum = 0;
		for(size_t index: range(X.size)) sum += X[index](x, y);
		if(sum) for(size_t index: range(Y.size)) Y[index](x, y) = X[index](x, y)/sum;
		else for(size_t index: range(Y.size)) Y[index](x, y) = 1./X.size;
	});
}
