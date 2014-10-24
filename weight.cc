#include "weight.h"

void Weight::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 3);
	forXY(Y[0].size, [&](uint x, uint y) {
		int2 A = int2(x,y);
		float mean[X.size]; float variance=0;
		for(size_t index: range(X.size)) {
			float N = 0, sum = 0;
			for(int dy: range(3)) for(int dx: range(3)) {
				int2 B = A+int2(dx-1, dy-1);
				if(!(B >= int2(0) && B < X[index].size)) continue;
				float b = X[index](B);
				sum += b;
				N += 1;
			}
			mean[index] = sum / N;
			float SSE = 0;
			for(int dy: range(3)) for(int dx: range(3)) {
				int2 B = A+int2(dx-1, dy-1);
				if(!(B >= int2(0) && B < X[index].size)) continue;
				float b = X[index](B);
				SSE += sq(mean[index] - b);
			}
			variance += SSE / N;
		}
		variance /= X.size;
		float intensity = (mean[0]+mean[1]+mean[2])/3;
		float saturation = (sq(mean[0]-intensity) + sq(mean[0]-intensity) + sq(mean[0]-intensity))/3;
		float delta[3] = {1-abs(mean[0]*2-1), 1-abs(mean[1]*2-1), 1-abs(mean[2]*2-1)};
		Y[0](A) =
				(delta[0] + delta[1] + delta[2])/3 + // Each component close to midrange
				sqrt(variance) + // Strong contrast (mean deviation over 3x3 window)
				sqrt(saturation)  // Saturated color (deviation of 3x3 mean over channels)
				;
	});
}

void SelectMaximum::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(Y.size == X.size);
	forXY(Y[0].size, [&](uint x, uint y) {
		int best = -1; float max = 0;
		for(size_t index: range(X.size)) { float v = X[index](x, y); if(v > max) { max = v, best = index; } }
		for(size_t index: range(Y.size)) Y[index](x, y) = 0;
		if(best>=0) Y[best](x, y) = 1;
	});
}

void NormalizeSum::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(Y.size == X.size);
	forXY(Y[0].size, [&](uint x, uint y) {
		float sum = 0;
		for(size_t index: range(X.size)) sum += X[index](x, y);
		if(sum) for(size_t index: range(Y.size)) Y[index](x, y) = X[index](x, y)/sum;
		else for(size_t index: range(Y.size)) Y[index](x, y) = 1./X.size;
	});
}