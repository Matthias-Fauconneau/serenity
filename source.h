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
	virtual ~Source() {}
};

/*/// Implements I::name for T
template<class I, class T> struct Name : virtual I {
	String name() const override {
		static String name = String(({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }));
		return copy(name);
	}
};*/

/// Splits a collection in groups
struct GroupSource : Source {
	virtual array<size_t> operator()(size_t groupIndex) = 0;
};

typedef ImageMapSource<ImageF> SourceImage;
typedef ImageMapSource<Image> SourceImageRGB;

/// Implicit collection of images
struct ImageSource : Source {
	/// Name
	virtual String name() const abstract;
	/// Number of outputs per image index (aka channels, components)
	virtual int outputs() const abstract;
	/// Folder to be used for persistent data (cache, parameters)
	virtual const Folder& folder() const abstract;
    virtual int2 maximumSize() const abstract;
	virtual String elementName(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;
	virtual const map<String, String>& properties(size_t index) const abstract;
	virtual SourceImage image(size_t /*index*/, int outputIndex, int2 unused size=0, bool unused noCacheWrite = false) {
		error(name(), "does not implement 32bit linear image["+str(outputIndex)+']');
	}
	virtual SourceImageRGB image(size_t /*index*/, int2 unused size = 0, bool unused noCacheWrite = false) {
		error(name(), "does not implement 8bit sRGB image");
	}
};

struct ImageGroupSource : Source {
	virtual String name() const abstract;
	virtual int outputs() const  abstract;
	virtual const Folder& folder() const  abstract;
	virtual int2 maximumSize() const  abstract;
	virtual String elementName(size_t groupIndex) const abstract;
	virtual int64 time(size_t groupIndex)  abstract;
	virtual int2 size(size_t groupIndex) const abstract;
	virtual array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size, bool noCacheWrite = false)  abstract;
};
