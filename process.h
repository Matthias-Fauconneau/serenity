#pragma once
#include "source.h"
#include "operation.h"

// ProcessedSource

/// Provides outputs of an \a operation on a \a source
struct ProcessedGenericSource : virtual GenericImageSource {
	GenericImageSource& source;
    ImageOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	ProcessedGenericSource(GenericImageSource& source, ImageOperation& operation) : source(source), operation(operation) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(source.name(), operation.name()); }
	size_t count(size_t need = 0) override { return source.count(need); }
    int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return max(source.time(index), operation.time()); }
    int2 size(size_t index) const override { return source.size(index); }
};

struct ProcessedSource : ProcessedGenericSource, ImageSource {
	ImageSource& source;
	ProcessedSource(ImageSource& source, ImageOperation& operation)
		: ProcessedGenericSource(source, operation), source(source) {}

	size_t outputs() const override { return operation.outputs(); }
	/// Returns processed linear image
	SourceImage image(size_t imageIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;
};

struct sRGBSource : ImageRGBSource {
	ImageSource& source;
	Folder cacheFolder {"sRGB", source.folder(), true};
	sRGBSource(ImageSource& source) : source(source) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(source.name(), "sRGB"); }
	size_t count(size_t need = 0) override { return source.count(need); }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return source.time(index); }
	int2 size(size_t index) const override { return source.size(index); }

	/// Returns processed sRGB image
	virtual SourceImageRGB image(size_t index, int2 size, bool noCacheWrite) override;
};

generic struct ProcessedSourceT : T, ProcessedSource {
	ProcessedSourceT(ImageSource& source) : ProcessedSource(source, *this) {}
};

/// Evaluates an image for each group
struct ProcessedGroupImageSource : ProcessedGenericSource, ImageSource {
	ImageGroupSource& source;
	ImageOperation& operation;
	Folder cacheFolder;
	ProcessedGroupImageSource(ImageGroupSource& source, ImageOperation& operation)
		: ProcessedGenericSource(source, operation), source(source), operation(operation), cacheFolder(operation.name(), source.folder(), true) {}

	size_t outputs() const override { return operation.outputs(); }
	SourceImage image(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;
};

generic struct ProcessedGroupImageSourceT : T, ProcessedGroupImageSource {
	ProcessedGroupImageSourceT(ImageGroupSource& source) : ProcessedGroupImageSource(source, *this) {}
};

// ProcessedImageGroupSource

/// Returns image groups from an image source grouped by a group source
struct ProcessedImageGroupSource : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	ProcessedImageGroupSource(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
	size_t count(size_t need=0) override { return groups.count(need); }
	String name() const override { return source.name(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t groupIndex) override { return max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })); }
	String elementName(size_t groupIndex) const override;
	int2 size(size_t groupIndex) const override;

	size_t outputs() const override { return source.outputs(); }
	size_t groupSize(size_t groupIndex) const { return groups(groupIndex).size; }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;
};

// ProcessedGroupImageGroupSource

/// Evaluates an operation on every image of an image group
struct ProcessedGroupImageGroupSource : ImageGroupSource {
	ImageGroupSource& source;
	ImageOperation& operation;
	Folder cacheFolder;
	ProcessedGroupImageGroupSource(ImageGroupSource& source, ImageOperation& operation) : source(source), operation(operation) {}

	size_t count(size_t need=0) override { return source.count(need); }
	String name() const override { return source.name(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t index) override { return source.time(index); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int2 size(size_t index) const override { return source.size(index); }

	size_t outputs() const override { return operation.outputs(); }
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;
};

generic struct ProcessedGroupImageGroupSourceT : T, ProcessedGroupImageGroupSource {
	ProcessedGroupImageGroupSourceT(ImageGroupSource& source) : ProcessedGroupImageGroupSource(source, *this) {}
};
