#pragma once
#include "core/image.h"

template<Type F, Type... S> void forXY(int2 size, F function, const S&... sources) {
	parallel_chunk(size.y, [&](uint, uint64 start, uint64 chunkSize) {
		for(size_t y: range(start, start+chunkSize)) for(size_t x: range(size.x)) {
			function(x, y, sources[y*sources.stride + x]...);
		}
	});
}

// -- ImageF --

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
	ImageF(){}
	ImageF(buffer<float>&& data, int2 size, size_t stride) : buffer(::move(data)), size(size), stride(stride) {
		assert_(buffer::size==size_t(size.y*stride), buffer::size, size, stride);
	}
	ImageF(int width, int height) : buffer(height*width), width(width), height(height), stride(width) {
		assert_(size>int2(0), size, width, height);
	}
	ImageF(int2 size) : ImageF(size.x, size.y) {}

	explicit operator bool() const { return data && width && height; }
	inline float& operator()(size_t x, size_t y) const {assert(x<width && y<height, x, y); return at(y*stride+x); }
	inline float& operator()(int2 p) const { return operator()(p.x, p.y); }

	union {
		int2 size = 0;
		struct { uint width, height; };
	};
	size_t stride = 0;
};
inline ImageF copy(const ImageF& o) {
	if(o.width == o.stride) return ImageF(copy((const buffer<float>&)o), o.size, o.stride);
	ImageF target(o.size);
	for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
	return target;
}
inline String str(const ImageF& o) { return strx(o.size); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline ImageF share(const ImageF& o) { return ImageF(unsafeRef(o), o.size, o.stride); }

/// Returns a cropped weak reference to \a image (unsafe if referenced image is freed)
inline ImageF crop(const ImageF& source, int2 origin, int2 size) {
	origin = clip(int2(0), origin, source.size);
	size = min(size, source.size-origin);
	return ImageF(buffer<float>(source.begin()+origin.y*source.stride+origin.x, size.y*source.stride, 0), size, source.stride);
}

// -- Applications

template<Type F, Type... S> void apply(const ImageF& target, F function, const S&... sources) {
	for(int2 size: ref<int2>{sources.size...}) assert_(target.size == size, target.size, sources.size...);
	parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
		for(size_t y: range(start, start+chunkSize)) for(size_t x: range(target.size.x)) {
			target[y*target.stride + x] = function(sources[y*sources.stride + x]...);
		}
	});
}
template<Type F> ImageF apply(ImageF&& y, const ImageF& a, const ImageF& b, F function) { apply(y, function, a, b); return move(y); }
template<Type F> ImageF apply(const ImageF& a, const ImageF& b, F function) { return apply(a.size, a, b, function); }

inline void max(const ImageF& y, const ImageF& a, const ImageF& b) { apply(y, [](float a, float b){ return max(a, b);}, a, b); }
inline void min(const ImageF& y, const ImageF& a, const ImageF& b) { apply(y, [](float a, float b){ return min(a, b);}, a, b); }
inline ImageF min(ImageF&& y, const ImageF& a, const ImageF& b) { min(y, a, b); return move(y); }
inline ImageF min(const ImageF& a, const ImageF& b) { return min(a.size, a, b); }

inline ImageF operator-(const ImageF& a, const ImageF& b) { return apply(a, b, [](float a, float b){ return a - b;}); }
inline ImageF operator/(const ImageF& a, const ImageF& b) { return apply(a, b, [](float a, float b){ return a / b;}); }

template<Type F, Type... S> void applyXY(const ImageF& target, F function, const S&... sources) {
	parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
		for(size_t y: range(start, start+chunkSize)) for(size_t x: range(target.size.x)) {
			target[y*target.stride + x] = function(x, y, sources[y*sources.stride + x]...);
		}
	});
}

// -- sRGB --

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

/// Converts an sRGB component to linear float
void linear(mref<float> target, ref<byte4> source, int component);
inline ImageF linear(ImageF&& target, const Image& source, int component) { linear(target, source, component); return move(target); }
inline ImageF linear(const Image& source, int component) { return linear(source.size, source, component); }

void sRGB(mref<byte4> target, ref<float> value);
inline Image sRGB(const ImageF& value) {
	Image sRGB (value.size); ::sRGB(sRGB, value); return sRGB;
}

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red);
inline Image sRGB(const ImageF& blue, const ImageF& green, const ImageF& red) {
	Image sRGB (blue.size); ::sRGB(sRGB, blue, green, red); return sRGB;
}

// -- Resample --

/// Downsamples linear float image by a factor 2
ImageF downsample(ImageF&& target, const ImageF& source);
inline ImageF downsample(const ImageF& source) { return downsample(source.size/2, source); }

// -- Convolution --

void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride);

/// Selects image (signal) components of scale (frequency) below threshold
/// Applies a gaussian blur
void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius=0);
inline ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma) { gaussianBlur(target, source, sigma); return move(target); }
inline ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }

#if 0
// -- Crop copy convert --

/// Copies rectangle \a size around \a origin from \a (red+green+blue) into \a target
static void crop(const ImageF& target, const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin) {
	parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
		assert_(origin >= int2(0) && origin+target.size <= red.size, withName(origin, target.size, red.size) );
		size_t offset = origin.y*red.stride + origin.x;
		for(size_t y: range(start, start+chunkSize)) {
			for(size_t x: range(target.size.x)) {
				size_t index = offset + y*red.stride + x;
				target[y*target.stride + x] = red[index] + green[index] + blue[index];
			}
		}
	});
}
inline ImageF crop(ImageF&& target, const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin) {
	crop(target, red, green, blue, origin); return move(target);
}
inline ImageF crop(const ImageF& red, const ImageF& green, const ImageF& blue, int2 origin, int2 size) {
	return crop({size,"crop"}, red, green, blue, origin);
}
#endif

// -- Insert --

/// Copies \a source with \a crop inserted at \a origin into \a target
static void insert(const ImageF& target, const ImageF& source, const ImageF& crop, int2 origin) {
	target.copy(source); // Assumes crop is small before source
	assert_(origin >= int2(0) && origin+crop.size <= target.size, origin, crop.size, target.size);
	parallel_chunk(crop.size.y, [&](uint, uint64 start, uint64 chunkSize) {
		size_t offset = origin.y*target.stride + origin.x;
		for(size_t y: range(start, start+chunkSize)) {
			for(size_t x: range(crop.size.x)) {
				size_t index = offset + y*target.stride + x;
				target[index] = crop[y*crop.stride + x];
			}
		}
	});
}
inline ImageF insert(ImageF&& target, const ImageF& source, const ImageF& crop, int2 origin) {
	insert(target, source, crop, origin); return move(target);
}
inline ImageF insert(const ImageF& source, const ImageF& crop, int2 origin) { return insert(source.size, source, crop, origin); }

// -- Reduction - argmin --

inline int2 argmin(const ImageF& source) {
	struct C { int2 position=0; float value=inf; };
	C minimums[threadCount];
	mref<C>(minimums).clear(); // Some threads may not iterate
	parallel_chunk(source.size.y, [&](uint id, uint64 start, uint64 chunkSize) {
		C min;
		for(size_t y: range(start, start+chunkSize)) {
			for(size_t x: range(source.size.x)) { float v = source(x,y); if(v < min.value) min = C{int2(x,y), v}; }
		}
		minimums[id] = min;
	});
	C min;
	for(C v: minimums) { if(v.value < min.value) min = v; }
	return min.position;
}

// -- Reduction - sum --

template<Type A, Type F, Type... Ss> A sumXY(int2 size, F apply, A initialValue) {
	assert_(size);
	if(size.y <= 16) {
		A accumulator = initialValue;
		for(size_t y: range(size.y)) for(size_t x: range(size.x)) accumulator += apply(x, y);
		return accumulator;
	}
	A accumulators[threadCount];
	mref<A>(accumulators).clear(initialValue); // Some threads may not iterate
	parallel_chunk(size.y, [&](uint id, uint64 start, uint64 chunkSize) {
		A accumulator = initialValue;
		for(size_t y: range(start, start+chunkSize)) for(size_t x: range(size.x)) accumulator += apply(x, y);
		accumulators[id] += accumulator;
	});
	return ::sum<A>(accumulators);
}

// -- SSE --

inline double SSE(const ImageF& A, const ImageF& B, int2 offset) {
	assert_(A.size == B.size);
	int2 size = A.size - abs(offset);
	assert_(size.x && size.y, size);
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
