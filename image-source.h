#pragma once
#include "file.h"
#include "variant.h" // fromInt2
#include "image.h"
#include "function.h"
#include "time.h"
#include "cache.h"

typedef ImageMapTarget<ImageF> TargetImage;
typedef ImageMapSource<ImageF> SourceImage;
typedef ImageMapTarget<Image> TargetImageRGB;
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
    virtual SourceImage image(size_t /*index*/, uint /*component*/, int2 unused hint=0) const { error("Unimplemented"); }
    virtual SourceImageRGB image(size_t /*index*/, int2 unused hint = 0) const { error("Unimplemented"); }
};

/*// Fits size to hint
inline int2 fit(int2 size, int2 hint) {
    if(!hint) return size; // No hint
    if(hint >= size) return size; // Larger hint
    return hint.x*size.y < hint.y*size.x ?
                int2(hint.x, ((hint.x+size.x-1)/size.x)*size.y) : // Fits width
                int2(((hint.y+size.y-1)/size.y)*size.x, hint.y) ; // Fits height
}*/
