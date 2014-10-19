#pragma once
#include "image.h"
#include <typeinfo>
#include "time.h"

/// Operation on an image
struct ImageOperation {
    virtual string name() const abstract;
    virtual int64 time() const abstract;
	virtual int inputs() const { return 3; }
	virtual int outputs() const { return 3; }
	virtual void apply1(const ImageF& /*Y*/, const ImageF& /*A*/, const ImageF& /*B*/) const { error(2, 1, inputs(), outputs()); }
	virtual void apply1(const ImageF& /*Y*/, const ImageF& /*R*/, const ImageF& /*G*/, const ImageF& /*B*/) const { error(3, 1, inputs(), outputs()); }
	// TODO: buffer<ImageF> return -> ref<ImageF> target
	virtual buffer<ImageF> apply3(const ImageF& /*R*/, const ImageF& /*G*/, const ImageF& /*B*/) const { error(3, 3, inputs(), outputs()); }
};

generic struct ImageOperationT : ImageOperation {
    string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
    int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};
