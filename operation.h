#pragma once
#include "time.h"
#include "image.h"

struct Operation {
	virtual string name() const abstract;
	virtual int64 time() const abstract;
};

generic struct OperationT : virtual Operation {
	//string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
	int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct ImageOperation : virtual Operation {
	virtual size_t inputs() const abstract;
	virtual size_t outputs() const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const abstract;
};

struct ImageOperation1 : ImageOperation {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 1);
		apply(Y[0], X[0]);
	}
};

struct ImageOperation21 : ImageOperation {
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 2);
		for(auto& x: X) assert_(x.size == Y[0].size);
		apply(Y[0], X[0], X[1]);
	}
};

struct ImageOperation31 : ImageOperation {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 3);
		for(auto& x: X) assert_(x.size == Y[0].size);
		apply(Y[0], X[0], X[1], X[2]);
	}
};

struct ImageGroupOperation : ImageOperation {
	size_t inputs() const override { return 0; } // Varying
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const abstract;
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		for(auto& x: X) assert_(x.size == Y[0].size);
		apply(Y[0], X);
	}
};
