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
struct GenericImageOperation : virtual Operation {
	virtual size_t inputs() const abstract;
	virtual size_t outputs() const abstract;
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct ImageOperation : virtual GenericImageOperation {
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const abstract;
};

struct ImageOperation1 : ImageOperation {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 1);
		apply(Y[0], X[0]);
	}
};

struct ImageOperation21 : ImageOperation {
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 2);
		for(auto& x: X) assert_(x.size == Y[0].size, Y, X, name());
		apply(Y[0], X[0], X[1]);
	}
};

struct ImageOperation31 : ImageOperation {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 3);
		for(auto& x: X) assert_(x.size == Y[0].size, Y, X, name());
		apply(Y[0], X[0], X[1], X[2]);
	}
};

// Basic ImageOperation implementations

struct Subtract : ImageOperation21, OperationT<Subtract> {
	string name() const override { return "[subtract]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const override;
};

struct Multiply : ImageOperation21, OperationT<Multiply> {
	string name() const override { return "[multiply]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const override;
};

struct Divide : ImageOperation21, OperationT<Divide> {
	string name() const override { return "[divide]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const override;
};

/// Averages 3 components
struct Intensity : ImageOperation31, OperationT<Intensity> {
	string name() const override { return "[intensity]"; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const override;
};

struct LowPass : ImageOperation1, OperationT<LowPass> {
	string name() const override { return "[low]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};

/*struct HighPass : ImageOperation1, OperationT<HighPass> {
	string name() const override { return "[high]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};

struct BandPass : ImageOperation1, OperationT<BandPass> {
	string name() const override { return "[band]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};*/

/// Normalizes mean and deviation
struct Normalize : ImageOperation1, OperationT<Normalize> {
	string name() const override { return "[normalize]"; }
	void apply(const ImageF& Y, const ImageF& X) const override;
};
