#pragma once
#include "source.h"

struct Transform {
	int2 size, offset;
	Transform(int2 size, int2 offset=0) : size(size), offset(offset) { assert_(size); }
	/// Returns scaled source coordinates for the given scaled target coordinates
	int2 nearest(int2 target, int2 size) const {
		assert_(size == this->size, size, this->size);
		return target + offset;
	}
	/// Returns scaled source coordinates for the given scaled target coordinates
	vec2 operator()(int2 target, int2 size) const { return vec2(nearest(target, size)); }
	/// Returns top left corner of inscribed rectangle
	int2 min(int2 size) const { assert_(size == this->size, size, this->size); return -offset; }
	/// Returns bottom right corner of inscribed rectangle
	int2 max(int2 size) const { assert_(size == this->size, size, this->size); return size-offset; }
};
String str(const Transform& o) { return str(o.size, o.offset); }
template<> Transform parse<Transform>(TextData& s) {
	int2 size = parse<int2>(s); s.whileAny(' '); int2 offset = parse<int2>(s); return {size, offset};
}
bool operator ==(const Transform& a, const Transform& b) { return a.offset*b.size == b.offset*a.size; }

/// Composes transforms
Transform operator *(const Transform& a, const Transform& b) {
	assert_(a.size == b.size);
	return {a.size, a.offset+b.offset};
}

struct TransformGroupSource : Source {
	virtual array<Transform> operator()(size_t groupIndex, int2 size=0) abstract;
};

/// Evaluates transforms for a group of images
struct ImageTransformGroupOperator : virtual Operator {
	virtual array<Transform> operator()(ref<ImageF>) const abstract;
};

struct ImageGroupTransformOperation : TransformGroupSource {
	ImageGroupSource& source;
	ImageTransformGroupOperator& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ImageGroupTransformOperation(ImageGroupSource& source, ImageTransformGroupOperator& operation) :
		source(source), operation(operation) {}

	virtual size_t count(size_t need=0) { return source.count(need); }
	virtual int64 time(size_t index) { return max(operation.time(), source.time(index)); }

	array<Transform> operator()(size_t groupIndex, int2 size) override {
		assert_(source.outputs()==1);
		if(!size) size = source.maximumSize()/16;
		array<SourceImage> images = source.images(groupIndex, 0, size);
		return parseArray<Transform>(cache(cacheFolder, source.elementName(groupIndex), strx(size), time(groupIndex), [&]() {
			return str(operation(apply(images, [](const SourceImage& x){ return share(x); })));
		}, false));
	}
};

generic struct ImageGroupTransformOperationT : T, ImageGroupTransformOperation {
	ImageGroupTransformOperationT(ImageGroupSource& source) : ImageGroupTransformOperation(source, *this) {}
};

/// Samples \a transform of \a source using nearest neighbour
void sample(const ImageF& target, const ImageF& source, Transform transform, int2 min, int2 max) {
	assert_(target.size == max-min);
	applyXY(target, [&](int x, int y) {
		int2 s = int2(round(transform(min+int2(x,y), source.size)));
		if(!(s >= int2(0) && s < source.size)) return 0.f;
		//assert_(s >= int2(0) && s < source.size, s, source.size);
		return source(s);
	});
}
ImageF sample(ImageF&& target, const ImageF& source, Transform transform, int2 min, int2 max) { sample(target, source, transform, min, max); return move(target); }
ImageF sample(const ImageF& source, Transform transform, int2 min, int2 max) { return sample(max-min, source, transform, min, max); }

//FIXME: reuse UnaryImageGroupSource
struct SampleImageGroupOperation : ImageGroupSource {
	ImageGroupSource& source;
	TransformGroupSource& transform;
	Folder cacheFolder {"[sample]", source.folder(), true};
	SampleImageGroupOperation(ImageGroupSource& source, TransformGroupSource& transform)
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

	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size=0, bool noCacheWrite = false) override {
		auto images = source.images(groupIndex, componentIndex, size, noCacheWrite);
		auto transforms = transform(groupIndex, size);
#if CROP // CROP
		int2 min = ::max(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 max =::min(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
#elif 1 // EXTEND X CROP Y
		int2 minmin = ::min(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 maxmin = ::max(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 minmax =::min(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
		int2 maxmax =::max(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
		int2 min (minmin.x, maxmin.y);
		int2 max (maxmax.x, minmax.y);
#elif KEEP // KEEP
		int2 min = 0; //::max(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 max = images[0].size; //::min(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
#else //EXTEND (FIXME: need to allocate larger image when cached
		int2 min = ::min(apply(transforms,[&](const Transform& t){ return t.min(images[0].size); }));
		int2 max =::max(apply(transforms,[&](const Transform& t){ return t.max(images[0].size); }));
#endif
		return apply(images.size, [&](size_t index) -> SourceImage { return sample(images[index], transforms[index], min, max); });
	}
};
