#include "weight.h"

void Exposure::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 3);
	//int2 center = Y[0].size/2; float sqRadius = sq(min(Y[0].size.x, Y[0].size.y)/2); //sq(center);
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
			float mean = sum / N;
			delta[index] = 1-gaussian(mean-1./2, 1./3);
		}
		float exposure = (delta[0] + delta[1] + delta[2])/3;
		//float mask = max(0.f, 1 - sq(A-center)/sqRadius); // Prevents splits near border
		//Y[0](A) = mask * exposure;
		Y[0](A) = exposure;
	});
}

/*void Saturation::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 3);
	forXY(Y[0].size, [&](uint x, uint y) {
		int2 A = int2(x,y);
		float mean[X.size];
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
		}
		float intensity = (mean[0]+mean[1]+mean[2])/3;
		float saturation = (sq(mean[0]-intensity) + sq(mean[0]-intensity) + sq(mean[0]-intensity))/3;
		Y[0](A) = sqrt(saturation);  // Saturated color (deviation of 3x3 mean over channels)
	});
}

void Contrast::apply(ref<ImageF> Y, ref<ImageF> X) const {
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
		Y[0](A) =	sqrt(variance); // Strong contrast (mean deviation over 3x3 window)
	});
}

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
		float delta[3];
		for(size_t i: range(3)) delta[i] = 1-gaussian(mean[i]-1./2, 1./3); // Half radius triangle (negative outside 1/4
		float intensity = (mean[0]+mean[1]+mean[2])/3;
		float saturation = (sq(mean[0]-intensity) + sq(mean[0]-intensity) + sq(mean[0]-intensity))/3;
		variance /= X.size;
		Y[0](A) =
				(delta[0] + delta[1] + delta[2])/3 + // Each component close to midrange
				sqrt(saturation) +  // Saturated color (deviation of 3x3 mean over channels)
				sqrt(variance) // Strong contrast (mean deviation over 3x3 window)
				;
	});
}*/

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
