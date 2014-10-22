#pragma once
#include "operation.h"

struct ImageGroupOperation : ImageOperation {
	size_t inputs() const override { return 0; } // Varying
	size_t outputs() const override { return 0; } // Varying (default)
};

struct ImageGroupOperation1 : ImageGroupOperation {
	size_t inputs() const override { return 0; } // Varying
	size_t outputs() const override { return 1; } // Fixed
	virtual void apply(const ImageF& Y, ref<ImageF> X) const abstract;
	void apply(ref<ImageF> Y, ref<ImageF> X) const override {
		assert_(Y.size == 1);
		for(auto& x: X) assert_(x.size == Y[0].size, Y, X, name());
		apply(Y[0], X);
	}
};

// Basic ImageGroupOperation implementations

/// Averages together all images in an image group
struct Mean : ImageGroupOperation1, OperationT<Mean> {
	string name() const override { return "[mean]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; }))/X.size; });
	}
};

/// Sums together all images in an image group
struct Sum : ImageGroupOperation1, OperationT<Sum> {
	string name() const override { return "[sum]"; }
	virtual void apply(const ImageF& Y, ref<ImageF> X) const {
		parallel::apply(Y, [&](size_t index) { return sum(::apply(X, [index](const ImageF& x) { return x[index]; })); });
	}
};
