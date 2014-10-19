#pragma once
#include <typeinfo>
#include "time.h"
#include "image.h"

struct Operation {
	virtual string name() const abstract;
	virtual int64 time() const abstract;
};

generic struct OperationT : virtual Operation {
	string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
	int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct ImageOperation : virtual Operation {
	virtual int inputs() const abstract;
	virtual int outputs() const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const abstract;
};

struct ImageOperation31 : ImageOperation{
	int inputs() const override { return 3; }
	int outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 3);
		for(auto& x: X) assert_(x.size == Y[0].size);
		apply(Y[0], X[0], X[1], X[2]);
	}
};

/// Operates on a varying number of images to provide a given number of outputs
struct ImageGroupOperation : virtual Operation {
	virtual int outputs() const abstract;
	virtual void apply(ref<ImageF>, ref<ImageF>) const = 0;
};

struct ImageGroupOperation1 : ImageGroupOperation {
	int outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		for(auto& x: X) assert_(x.size == Y[0].size);
		apply(Y[0], X);
	}
};
