#include "image.h"

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
void __attribute((constructor(1001))) generate_sRGB_forward() {
	for(uint index: range(sizeof(sRGB_forward))) {
		real linear = (real) index / (sizeof(sRGB_forward)-1);
		real sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
		assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))<exp2(-50));
		sRGB_forward[index] = round(0xFF*sRGB);
	}
}

float sRGB_reverse[0x100];
void __attribute((constructor(1001))) generate_sRGB_reverse() {
	for(uint index: range(0x100)) {
		real sRGB = (real) index / 0xFF;
		real linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
		assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))<exp2(-50));
		sRGB_reverse[index] = linear;
		assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
	}
}

void linear(mref<float> target, ref<byte4> source, int component) {
	/***/ if(component==0) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[0]]; }, source);
	else if(component==1) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[1]]; }, source);
	else if(component==2) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[2]]; }, source);
	else error(component);
}

static uint8 sRGB(float v) {
	assert_(isNumber(v), v);
	v = ::min(1.f, v); // Saturates
	v = ::max(0.f, v); // Clips
	uint linear12 = 0xFFF*v;
	assert_(linear12 < 0x1000);
	return sRGB_forward[linear12];
}
void sRGB(mref<byte4> target, ref<float> source) {
	parallel::apply(target, [](float value) { uint8 v=sRGB(value); return byte4(v,v,v, 0xFF); }, source);
}
void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red) {
	parallel::apply(target, [=](float b, float g, float r) { return byte4(sRGB(b), sRGB(g), sRGB(r), 0xFF); }, blue, green, red);
}

// -- Resample --

ImageF downsample(ImageF&& target, const ImageF& source) {
	assert_(target.size == source.size/2, target.size, source.size);
	for(uint y: range(target.height)) for(uint x: range(target.width))
		target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
	return move(target);
}

// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
static void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
	int N = radius+1+radius;
	assert_(N < 1024, N);
	chunk_parallel(height, [=](uint, size_t y) {
		const float* line = source + y * sourceStride;
		float* targetColumn = target + y;
		if(width >= radius+1) {
			for(int x: range(-radius,0)) {
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
				targetColumn[(x+radius)*targetStride] = sum;
			}
			for(int x: range(0,width-2*radius)) {
				float sum = 0;
				const float* span = line + x;
				for(int dx: range(N)) sum += kernel[dx] * span[dx];
				targetColumn[(x+radius)*targetStride] = sum;
			}
			for(int x: range(width-2*radius,width-radius)){
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(x+dx-(width-1))];
				targetColumn[(x+radius)*targetStride] = sum;
			}
		} else {
			for(int x: range(-radius, width-radius)) {
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(abs(x+dx)-(width-1))];
				targetColumn[(x+radius)*targetStride] = sum;
			}
		}
	});
}

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
	assert_(sigma > 0);
	if(!radius) radius = ceil(3*sigma);
	size_t N = radius+1+radius;
	assert_(int2(radius+1) <= source.size, sigma, radius, N, source.size);
	float kernel[N];
	for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
	real sum = ::sum<real>(ref<float>(kernel,N)); assert_(sum, ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
	buffer<float> transpose (target.height*target.width);
	convolve(transpose.begin(), source.begin(), kernel, radius, source.width, source.height, source.stride, source.height);
	assert_(source.size == target.size);
	convolve(target.begin(),  transpose.begin(), kernel, radius, target.height, target.width, target.height, target.stride);
}
