#pragma once
#include "operation.h"

/*struct Substract : ImageOperation, OperationT<Substract> {
	int inputs() const override { return 2; }
	int outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};*/

struct Subtract : ImageGroupOperation, OperationT<Subtract> {
	int outputs() const override { return 1; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};
