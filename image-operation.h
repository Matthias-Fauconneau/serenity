#pragma once
#include "image.h"
#include <typeinfo>
#include "time.h"

/// Operation on an image
struct ImageOperation {
    virtual string name() const abstract;
    virtual int64 version() const abstract;
    virtual void apply(const ImageF& target, const ImageF& source, uint component) const abstract;
};

generic struct ImageOperationT : ImageOperation {
    string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.integer(); s.identifier(); }); return name; }
    int64 version() const override { return parseDate(__TIMESTAMP__)*1000000000l;; }
};
