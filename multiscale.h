#pragma once
#include "operator.h"

struct LowPass : ImageOperator1, OperatorT<LowPass> { void apply(const ImageF& Y, const ImageF& X) const override; };

/// Splits in bands
struct WeightFilterBank : ImageOperator, OperatorT<WeightFilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

/// Splits in bands
struct FilterBank : ImageOperator, OperatorT<FilterBank> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override;
};

#include "source.h"
struct JoinOperation : ImageSource {
	array<ImageSource*> sources;
	Folder cacheFolder {"Join", sources[0]->folder() /*FIXME: MRCA*/, true};
	JoinOperation(ref<ImageSource*> sources) : sources(sources) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(apply(sources,[](const ImageSource* source){ return source->name(); })); };
	size_t count(size_t need = 0) override { return sources[0]->count(need); }
	int2 maximumSize() const override { return sources[0]->maximumSize(); }
	String elementName(size_t index) const override { return sources[0]->elementName(index); }
	int64 time(size_t index) override { return max(apply(sources,[=](ImageSource* source){ return source->time(index); })); }
	int2 size(size_t index) const override { return sources[0]->size(index); }

	size_t outputs() const override { return sources.size; }
	SourceImage image(size_t index, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override {
		assert_(sources[componentIndex]->outputs()==1);
		log(sources[componentIndex]->folder().name());
		return sources[componentIndex]->image(index, 0, size, noCacheWrite);
	}
};
