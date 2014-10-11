#pragma once
#include "image.h"
#include <typeinfo>
#include "time.h"

/// Operation on an image
struct ImageOperation {
    virtual string name() const abstract;
    virtual string version() const abstract;
    virtual int64 time() const { return  0; }
    virtual void apply(ImageF& target, const ImageF& source, uint component) const abstract;
};

generic struct ImageOperationT : ImageOperation {
    string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
    string version() const override { return __DATE__ " " __TIME__; }
};
