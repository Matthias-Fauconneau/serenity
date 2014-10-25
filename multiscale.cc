#include "multiscale.h"

static void average(float* target, const float* source, int radius, int width, int height, uint sourceStride, uint targetStride) {
	assert_(radius < 2*width, radius, width);
	float N = radius+1+radius;
	chunk_parallel(height, [=](uint, size_t y) {
		const float* line = source + y * sourceStride;
		float* targetColumn = target + y;
		float sum = 0;
		for(int x: range(radius)) sum += 2*line[width-1-abs(x-(width-1))]; // Initalizes sum (double mirror condition)
		for(int x: range(0, width)) {
			sum += line[abs(width-1-abs(x+radius-(width-1)))]; // double mirror condition
			targetColumn[x*targetStride] = sum / N;
			sum -= line[width-1-abs(abs(x-radius)-(width-1))]; // double mirror condition
		}
	});
}

void boxBlur(const ImageF& target, const ImageF& source, int radius) {
	buffer<float> transpose (target.height*target.width, "transpose");
	average(transpose.begin(), source.begin(), radius, source.width, source.height, source.stride, source.height);
	assert_(source.size == target.size);
	average(target.begin(),  transpose.begin(), radius, target.height, target.width, target.height, target.stride);
}

void LowPass::apply(const ImageF& Y, const ImageF& X) const {
	//boxBlur(Y, X, 2*(X.size.y-1));
	const float largeScale = (min(X.size.x, X.size.y)-1)/6;
	gaussianBlur(Y, X, largeScale);
}

void WeightFilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& x = X[0];
	const float largeScale = (min(x.size.x, x.size.y)-1)/6;
	float octaveScale = largeScale / 4;
	for(size_t index: range(Y.size)) {
		gaussianBlur(Y[index], X[0], octaveScale); // Low pass weights up to octave
		octaveScale /= 2; // Next octave
	}
}

void FilterBank::apply(ref<ImageF> Y, ref<ImageF> X) const {
	assert_(X.size == 1);
	const ImageF& r = Y[Y.size-1];
	r.copy(X[0]); // Remaining octaves
	const float largeScale = (min(r.size.x, r.size.y)-1)/6;
	float octaveScale = largeScale / 4;
	for(size_t index: range(Y.size-1)) {
		gaussianBlur(Y[index], r, octaveScale); // Splits lowest octave
		parallel::sub(r, r, Y[index]); // Removes from remainder
		octaveScale /= 2; // Next octave
	}
	// Y[bandCount-1] holds remaining high octaves
}
