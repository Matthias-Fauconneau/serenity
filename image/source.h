#pragma once
#include "file.h"
#include "image/image.h"
#include "function.h"
#include "time.h"
#include "cache.h"

/// Implicit collection of elements
struct Source {
	/// Returns number of elements
	/// \a need is the minimum number to count for lazy evaluation
	virtual size_t count(size_t need=-1) abstract;
	/// Last modified time of element
	virtual int64 time(size_t index) abstract;
};

/// Splits a collection in groups
struct GroupSource : Source {
	virtual buffer<size_t> operator()(size_t groupIndex) = 0;
};

typedef ImageMapSource<ImageF> SourceImage;
typedef ImageMapSource<Image> SourceImageRGB;

/// Implicit collection of images
struct GenericImageSource : Source {
	/// Name
	virtual String name() const abstract;
	/// Path to be used for persistent data (cache, parameters))
	virtual String path() const abstract;
    virtual int2 maximumSize() const abstract;
	virtual String elementName(size_t index) const abstract;
	virtual int2 size(size_t index, int2 size=0) const abstract;

	virtual String toString() const { return name(); }
};

struct ImageSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual SourceImage image(size_t index, size_t componentIndex, int2 size, string parameters = "") abstract;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

struct ImageRGBSource : virtual GenericImageSource {
	virtual SourceImageRGB image(size_t index, int2 size = 0, string parameters = "") abstract;
};

struct GenericImageGroupSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual size_t groupSize(size_t groupIndex) const abstract;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

struct ImageGroupSource : virtual GenericImageGroupSource {
	virtual array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, string parameters = "") abstract;
};

struct ImageRGBGroupSource : virtual GenericImageGroupSource {
	virtual array<SourceImageRGB> images(size_t groupIndex, int2 size, string parameters = "") abstract;
};

struct PropertySource : virtual Source {
	virtual string at(size_t index) const abstract;
};

/// Forwards a component transparently across a single component group operation
struct ImageGroupForwardComponent : ImageGroupSource {
	ImageGroupSource& input;
	ImageGroupSource& target;
	struct ImageGroupForwardComponentSource : ImageGroupSource {
		ImageGroupForwardComponent& forward;
		ImageGroupForwardComponentSource(ImageGroupForwardComponent& forward) : forward(forward) {}
		size_t count(size_t need=0) { return forward.input.count(need); }
		int64 time(size_t index) { return forward.input.time(index); }
		String name() const { return forward.input.name(); }
		String path() const { return forward.input.path(); }
		int2 maximumSize() const { return forward.input.maximumSize(); }
		String elementName(size_t index) const { return forward.input.elementName(index); }
		int2 size(size_t index, int2 size=0) const { return forward.input.size(index, size); }
		size_t groupSize(size_t groupIndex) const { return forward.input.groupSize(groupIndex); }
		size_t outputs() const { return 1; }
		array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, string parameters = "") {
			assert_(componentIndex == 0);
			assert_(isInteger(parameters), parameters);
			return forward.input.images(groupIndex, parseInteger(parameters), size, parameters);
		}
	} source {*this};
	ImageGroupForwardComponent(ImageGroupSource& input, ImageGroupSource& target) : input(input), target(target) {}

    size_t count(size_t need=0) override { return target.count(need); }
    int64 time(size_t index) override { return target.time(index); }
    String name() const override { return target.name(); }
    String path() const override { return target.path(); }
    int2 maximumSize() const override { return target.maximumSize(); }
    String elementName(size_t index) const override { return target.elementName(index); }
    int2 size(size_t index, int2 size=0) const override { return target.size(index, size); }
    size_t groupSize(size_t groupIndex) const override { return target.groupSize(groupIndex); }
    size_t outputs() const override { return input.outputs(); }
	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, string parameters = "") override {
		assert_(!parameters);
		assert_(target.outputs()==1);
		return target.images(groupIndex, 0, size, str(componentIndex));
	}
};
