#pragma once
#include "source.h"
#include "operation.h"

// ProcessedSource

/// Provides outputs of an \a operation on a \a source
struct ProcessedSource : ImageSource {
    ImageSource& source;
    ImageOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedSource(ImageSource& source, ImageOperation& operation) : source(source), operation(operation) {}

	int outputs() const override { return operation.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(operation.name(), source.name()); }
	size_t count(size_t) override { return source.count(); }
    int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return max(operation.time(), source.time(index)); }
    const map<String, String>& properties(size_t index) const override { return source.properties(index); }
    int2 size(size_t index) const override { return source.size(index); }

	/// Returns processed linear image
	SourceImage image(size_t imageIndex, int outputIndex, int2 size = 0, bool noCacheWrite = false) override;
    /// Returns processed sRGB image
	virtual SourceImageRGB image(size_t index, int2 size, bool noCacheWrite) override;
};

generic struct ProcessedSourceT : T, ProcessedSource {
	ProcessedSourceT(ImageSource& source) : ProcessedSource(source, *this) {}
};

// ProcessedGroupImageSource

struct ProcessedGroupImageSource : ImageSource {
	ImageSource& source;
	GroupSource& groups;
	ImageGroupOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedGroupImageSource(ImageSource& source, GroupSource& groups, ImageGroupOperation& operation) :
		source(source), groups(groups), operation(operation) {}

	int outputs() const override { return operation.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(operation.name(), source.name()); }
	size_t count(size_t need=0) override { return groups.count(need); }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override {
		return str(apply(groups(groupIndex), [this](const size_t index) { return source.elementName(index); }));
	}
	int64 time(size_t groupIndex) override {
		return max(operation.time(), max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })));
	}
	const map<String, String>& properties(size_t unused groupIndex) const override {
		static map<String, String> empty;
		return empty; // Unimplemented
		//return str(apply(groups(groupIndex), [this](const size_t index) { return source.properties(index); }));
	}
	int2 size(size_t groupIndex) const override {
		auto sizes = apply(groups(groupIndex), [this](size_t index) { return source.size(index); });
		for(auto size: sizes) assert_(size == sizes[0]);
		return sizes[0];
	}

	SourceImage image(size_t groupIndex, int outputIndex, int2 unused size=0, bool unused noCacheWrite = false) override;
	SourceImageRGB image(size_t groupIndex, int2 size, bool noCacheWrite) override;
};
