#pragma once
#include "time.h"
#include "image.h"
#include "algorithm.h"
#include "simd.h"

struct Operator {
	virtual string name() const abstract;
	virtual int64 time() const abstract;
};

#include <typeinfo>
generic struct OperatorT : virtual Operator {
	string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
	int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct GenericImageOperator : virtual Operator {
	virtual size_t inputs() const abstract;
	virtual size_t outputs() const abstract;
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct ImageOperator : virtual GenericImageOperator {
	virtual void apply(ref<ImageF> Y, ref<ImageF> X) const abstract;
};

/// Operates on a predetermined number of images to provide a given number of outputs
struct ImageRGBOperator : virtual GenericImageOperator {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	virtual void apply(const Image& Y, const Image& X) const abstract;
};

struct ImageGroupOperator : ImageOperator {
	size_t inputs() const override { return 0; } // Varying
	size_t outputs() const override { return 0; } // Varying (default)
};

struct ImageOperator1 : ImageOperator {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	virtual void apply(const ImageF& Y, const ImageF& X) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		assert_(X.size == 1);
		apply(Y[0], X[0]);
	}
};

struct ImageGroupOperator1 : ImageGroupOperator {
	size_t inputs() const override { return 0; } // Varying
	size_t outputs() const override { return 1; } // Fixed
	virtual void apply(const ImageF& Y, ref<ImageF> X) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		for(auto& x: X) assert_(x.size == Y[0].size, Y, X, name());
		apply(Y[0], X);
	}
};

/// Averages 3 components
struct Intensity : ImageOperator, OperatorT<Intensity> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 1; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override { apply(Y[0], X[0], X[1], X[2]); }
};

/// Normalizes mean and deviation
struct Normalize : ImageOperator1, OperatorT<Normalize> {
	void apply(const ImageF& Y, const ImageF& X) const override;
};

/// Multiplies 2 components together
struct Multiply : ImageOperator, OperatorT<Multiply> {
	size_t inputs() const override { return 2; }
	size_t outputs() const override { return 1; }
	void apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override { apply(Y[0], X[0], X[1]); }
};

/// Sums together all images in an image group
struct Sum : ImageGroupOperator1, OperatorT<Sum> {
 void apply(const ImageF& Y, ref<ImageF> X) const override {
  assert(Y.ref::size%8 == 0);
  log("Sum");
  const uint64 align = 4*sizeof(float);
  assert_(uint64(Y.data)%align==0);
  log(hex((uint64)Y.data), Y.size, Y.ref::size, Y.capacity);
  for(const ImageF& x: X) {
   assert_(uint64(x.data)%align==0);
   log(hex((uint64)x.data), x.size, x.ref::size, x.capacity);
  }
  for(size_t i=0; i<Y.ref::size; i+=4) {
   v4sf sum = 0;
   for(const ImageF& x: X) sum += *(v4sf*)(x.data+i);
   *(v4sf*)(Y.data+i) = sum;
  }
  log("OK");
 }
};
