#pragma once
#include "image-source.h"
#include "image-operation.h"

struct ProcessedSource : ImageSource {
    ImageSource& source;
    ImageOperation& operation;
    ProcessedSource(ImageSource& source, ImageOperation& operation) :
        ImageSource(Folder(".",source.folder)), source(source), operation(operation) {}

    String name() const override { return str(operation.name(), source.name()); }
    size_t count() const override { return source.count(); }
    int2 maximumSize() const override { return source.maximumSize(); }
    String name(size_t index) const override { return source.name(index); }
    int64 time(size_t index) const override { return max(operation.time(), source.time(index)); }
    const map<String, String>& properties(size_t index) const override { return source.properties(index); }
    int2 size(size_t index) const override { return source.size(index); }

	/// Returns processed linear image
	SourceImage image(size_t index, int component, int2 size = 0, bool noCacheWrite = false) const override;
    /// Returns processed sRGB image
	virtual SourceImageRGB image(size_t index, int2 size, bool noCacheWrite) const override;
};

generic struct ProcessedSourceT : ProcessedSource {
	T operation;
	ProcessedSourceT(ImageSource& source) : ProcessedSource(source, operation) {}
};

/// Applies operation on two consecutive images
struct ProcessedSequence : ProcessedSource {
	using ProcessedSource::ProcessedSource;
	size_t count() const override { return source.count()-1; }
	String name(size_t index) const override { return source.name(index)+source.name(index+1); }
	int64 time(size_t index) const override { return max(ProcessedSource::time(index), source.time(index+1)); }
	const map<String, String>& properties(size_t) const override { error("Unimplemented properties"); }
	int2 size(size_t index) const override {
		assert_(ProcessedSource::size(index) ==ProcessedSource::size(index+1));
		return ProcessedSource::size(index);
	}
	/// Returns processed linear image
	SourceImage image(size_t index, int component, int2 size = 0, bool noCacheWrite = false) const override;
	SourceImageRGB image(size_t index, int2 size = 0, bool noCacheWrite = false) const override;
};

generic struct ProcessedSequenceT : ProcessedSequence {
	T operation;
	ProcessedSequenceT(ImageSource& source) : ProcessedSequence(source, operation) {}
};
