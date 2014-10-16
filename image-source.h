#pragma once
#include "file.h"
#include "variant.h" // fromInt2
#include "image.h"
#include "function.h"
#include "time.h"
#include "cache.h"

typedef ImageMapSource<ImageF> SourceImage;
typedef ImageMapSource<Image> SourceImageRGB;

/// Collection of images
struct ImageSource {
    Folder folder;
    ImageSource(Folder&& folder) : folder(move(folder)) {}
    virtual String name() const abstract;
    virtual size_t count() const abstract;
    virtual int2 maximumSize() const abstract;
    virtual String name(size_t index) const abstract;
    virtual int64 time(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;
    virtual const map<String, String>& properties(size_t index) const abstract;
    virtual SourceImage image(size_t /*index*/, int /*component*/, int2 unused hint=0) const { error("Unimplemented"); }
    virtual SourceImageRGB image(size_t /*index*/, int2 unused hint = 0) const { error("Unimplemented"); }
};
