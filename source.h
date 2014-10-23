#pragma once
#include "file.h"
#include "image.h"
#include "function.h"
#include "time.h"
#include "cache.h"

/// Implicit collection of elements
struct Source {
	/// Returns number of elements
	/// \a need is the minimum number to count for lazy evaluation
	virtual size_t count(size_t need=0) abstract;
	/// Last modified time of element
	virtual int64 time(size_t index) abstract;
};

/// Splits a collection in groups
struct GroupSource : Source {
	virtual array<size_t> operator()(size_t groupIndex) = 0;
};

typedef ImageMapSource<ImageF> SourceImage;
typedef ImageMapSource<Image> SourceImageRGB;

/// Implicit collection of images
struct GenericImageSource : Source {
	/// Name
	virtual String name() const abstract;
	/// Folder to be used for persistent data (cache, parameters)
	virtual const Folder& folder() const abstract;
    virtual int2 maximumSize() const abstract;
	virtual String elementName(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;

	virtual String toString() const { return name(); }
};

struct ImageSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual SourceImage image(size_t index, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) abstract;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

struct ImageRGBSource : virtual GenericImageSource {
	virtual SourceImageRGB image(size_t index, int2 size = 0, bool noCacheWrite = false) abstract;
};

struct GenericImageGroupSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual size_t groupSize(size_t groupIndex) const abstract;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

struct ImageGroupSource : virtual GenericImageGroupSource {
	virtual array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite = false)  abstract;
};

struct ImageRGBGroupSource : virtual GenericImageGroupSource {
	virtual array<SourceImageRGB> images(size_t groupIndex, int2 size, bool noCacheWrite = false)  abstract;
};
