#pragma once
#include "image.h"
#include <typeinfo>
#include "time.h"

/// Operation on an image
struct ImageOperation {
    virtual string name() const abstract;
    virtual int64 time() const abstract;
    virtual buffer<ImageF> apply(const ImageF& red, const ImageF& green, const ImageF& blue) const abstract;
};

generic struct ImageOperationT : ImageOperation {
    string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
    int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};
