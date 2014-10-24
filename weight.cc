#include "weight.h"

void Exposure::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 3);
	forXY(Y[0].size, [&](uint x, uint y) {
		int2 A = int2(x,y);
		float delta[X.size];
		for(size_t index: range(X.size)) {
			float N = 0, sum = 0;
			for(int dy: range(3)) for(int dx: range(3)) {
				int2 B = A+int2(dx-1, dy-1);
				if(!(B >= int2(0) && B < X[index].size)) continue;
				float b = X[index](B);
				sum += b;
				N += 1;
			}
			assert_(N);
			float mean = sum / N;
			delta[index] = 1-gaussian(mean-1./2, 1./3);
		}
		float exposure = (delta[0] + delta[1] + delta[2])/3;
		Y[0](A) = exposure;
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

void SmoothStep::apply(const ImageF& Y, const ImageF& X) const {
	 parallel::apply(Y, [](float x) { return 6*x*x*x*x*x - 15*x*x*x*x + 10*x*x*x; }, X);
}
