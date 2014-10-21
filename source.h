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
};

struct ImageSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual SourceImage image(size_t index, int outputIndex, int2 size = 0, bool noCacheWrite = false) abstract;
};

struct ImageRGBSource : virtual GenericImageSource {
	virtual SourceImageRGB image(size_t index, int2 size = 0, bool noCacheWrite = false) abstract;
};

struct ImageGroupSource : virtual GenericImageSource {
	/// Number of outputs per image index (aka channels, components)
	virtual size_t outputs() const abstract;
	virtual array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size, bool noCacheWrite = false)  abstract;
};
