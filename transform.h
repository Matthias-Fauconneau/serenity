#pragma once
#include "source.h"

struct Transform {
	vec2 offset;
	/// Returns normalized source coordinates for the given normalized target coordinates
	vec2 operator()(vec2 target) const { return target + offset; }
	/// Returns scaled source coordinates for the given scaled target coordinates
	vec2 operator()(int2 target, int2 size) const { return operator()(vec2(target)/vec2(size))*vec2(size); }
	/// Returns top left corner of inscribed rectangle
	int2 min(int2 size) const { return int2(ceil(-offset*vec2(size))); }
	/// Returns bottom right corner of inscribed rectangle
	int2 max(int2 size) const { return int2(floor(vec2(size)-offset*vec2(size))); }
};
// FIXME
static constexpr vec2 maximumImageSize = vec2(4000,3000);
String str(const Transform& o) { return str(o.offset*maximumImageSize); }
template<> Transform parse<Transform>(TextData& s) { return {parse<vec2>(s)/maximumImageSize}; }

bool operator ==(const Transform& a, const Transform& b) { return a.offset == b.offset; }

/// Composes transforms
Transform operator *(const Transform& a, const Transform& b) {
	return {a.offset+b.offset};
}

struct TransformGroupSource : Source {
	virtual array<Transform> operator()(size_t groupIndex, int2 size=0) abstract;
};

/// Evaluates transforms for a group of images
struct ImageTransformGroupOperation : virtual Operation {
	virtual array<Transform> operator()(ref<ImageF>) const abstract;
};

struct ProcessedImageTransformGroupSource : TransformGroupSource {
	ImageGroupSource& source;
	ImageTransformGroupOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedImageTransformGroupSource(ImageGroupSource& source, ImageTransformGroupOperation& operation) :
		source(source), operation(operation) {}

	virtual size_t count(size_t need=0) { return source.count(need); }
	virtual int64 time(size_t index) { return max(operation.time(), source.time(index)); }

	array<Transform> operator()(size_t groupIndex, int2 size) override {
		assert_(source.outputs()==1);
		if(!size) size = source.size(groupIndex)/4;
		array<SourceImage> images = source.images(groupIndex, 0, size);
		return parseArray<Transform>(cache(cacheFolder, source.elementName(groupIndex), strx(size), time(groupIndex), [&]() {
			return str(operation(apply(images, [](const SourceImage& x){ return share(x); })));
		}, false));
	}
};

generic struct ProcessedImageTransformGroupSourceT : T, ProcessedImageTransformGroupSource {
	ProcessedImageTransformGroupSourceT(ImageGroupSource& source) : ProcessedImageTransformGroupSource(source, *this) {}
};

/// Samples \a transform of \a source using nearest neighbour
void sample(const ImageF& target, const ImageF& source, Transform transform, int2 min, int2 max) {
	assert_(target.size == max-min);
	applyXY(target, [&](int x, int y) {
		int2 s = int2(round(transform(min+int2(x,y), source.size)));
		if(!(s >= int2(0) && s < source.size)) return 0.f;
		assert_(s >= int2(0) && s < source.size, s, source.size);
		return source(s);
	});
}
ImageF sample(ImageF&& target, const ImageF& source, Transform transform, int2 min, int2 max) { sample(target, source, transform, min, max); return move(target); }
ImageF sample(const ImageF& source, Transform transform, int2 min, int2 max) { return sample(max-min, source, transform, min, max); }

//FIXME: reuse ProcessedImageGroupSource
struct TransformSampleImageGroupSource : ImageGroupSource {
	ImageGroupSource& source;
	TransformGroupSource& transform;
	Folder cacheFolder {"[sample]", source.folder(), true};
	TransformSampleImageGroupSource(ImageGroupSource& source, TransformGroupSource& transform)
		: source(source), transform(transform) {}

	size_t count(size_t need=0) override { return source.count(need); }
	int64 time(size_t groupIndex) override { return max(source.time(groupIndex), transform.time(groupIndex)); }
	String name() const override { return str(source.name(), "[sample]"); }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override { return source.elementName(groupIndex); }
	int2 size(size_t groupIndex) const override { return source.size(groupIndex); }

	size_t outputs() const override { return source.outputs(); }
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }

	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size=0, bool noCacheWrite = false) override {
		auto images = source.images(groupIndex, outputIndex, size, noCacheWrite);
		auto transforms = transform(groupIndex, size);
		int2 min = ::max(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 max = ::min(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
		return apply(images.size, [&](size_t index) -> SourceImage { return sample(images[index], transforms[index], min, max); });
	}
};
