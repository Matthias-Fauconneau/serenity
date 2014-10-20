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

/// Evaluates an image for each group
struct ProcessedGroupImageSource : ImageSource {
	ImageGroupSource& source;
	ImageGroupOperation& operation;
	Folder cacheFolder;
	ProcessedGroupImageSource(ImageGroupSource& source, ImageGroupOperation& operation);

	int outputs() const override;
	const Folder& folder() const override;
	String name() const override;
	size_t count(size_t need=0) override;
	int2 maximumSize() const override;
	String elementName(size_t groupIndex) const override;
	int64 time(size_t groupIndex) override;
	const map<String, String>& properties(size_t unused groupIndex) const override;
	int2 size(size_t groupIndex) const override;

	SourceImage image(size_t groupIndex, int outputIndex, int2 unused size=0, bool unused noCacheWrite = false) override;
	SourceImageRGB image(size_t groupIndex, int2 size, bool noCacheWrite) override;
};
