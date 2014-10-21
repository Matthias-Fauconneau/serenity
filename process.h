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
	String name() const override { return str(source.name(), operation.name()); }
	size_t count(size_t) override { return source.count(); }
    int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return max(source.time(index), operation.time()); }
	//const map<String, String>& properties(size_t index) const override { return source.properties(index); }
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
	ProcessedGroupImageSource(ImageGroupSource& source, ImageGroupOperation& operation)
		: source(source), operation(operation), cacheFolder(operation.name(), source.folder(), true) {}

	int outputs() const override { return operation.outputs(); }
	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(source.name(), operation.name()); }
	size_t count(size_t need=0) override { return source.count(need); }
	int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t groupIndex) const override { return source.elementName(groupIndex); }
	int64 time(size_t groupIndex) override { return max(operation.time(), source.time(groupIndex)); }
	//const map<String, String>& properties(size_t unused groupIndex) const override;
	int2 size(size_t groupIndex) const override { return source.size(groupIndex); }

	SourceImage image(size_t groupIndex, int outputIndex, int2 unused size=0, bool unused noCacheWrite = false) override;
	SourceImageRGB image(size_t groupIndex, int2 size, bool noCacheWrite) override;
};

/// Returns image groups from an image source grouped by a group source
struct ProcessedImageGroupSource : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	ProcessedImageGroupSource(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
	size_t count(size_t need=0) override { return groups.count(need); }
	String name() const override { return source.name(); }
	int outputs() const override { return source.outputs(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t groupIndex) override { return max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })); }
	String elementName(size_t groupIndex) const override {
		return str(apply(groups(groupIndex), [this](const size_t imageIndex) { return source.elementName(imageIndex); }));
	}
	int2 size(size_t groupIndex) const override {
		auto sizes = apply(groups(groupIndex), [this](size_t imageIndex) { return source.size(imageIndex); });
		for(auto size: sizes) assert_(size == sizes[0], sizes);
		return sizes[0];
	}

	array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) override {
		return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, outputIndex, size, noCacheWrite); });
	}
};

/// Evaluates an operation on every image of an image group
struct ProcessedGroupImageGroupSource : ImageGroupSource {
	ImageGroupSource& source;
	ImageOperation& operation;
	Folder cacheFolder;
	ProcessedGroupImageGroupSource(ImageGroupSource& source, ImageOperation& operation) : source(source), operation(operation) {}

	size_t count(size_t need=0) override { return source.count(need); }
	String name() const override { return source.name(); }
	int outputs() const override { return source.outputs(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t index) override { return source.time(index); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int2 size(size_t index) const override { return source.size(index); }

	array<SourceImage> images(size_t groupIndex, int outputIndex, int2 size=0, bool noCacheWrite = false) override {
		assert_(operation.outputs() == 1);
		assert_(outputIndex == 0);
		array<array<SourceImage>> groupInputs;
		for(size_t inputIndex: range(operation.inputs())) groupInputs.append( source.images(groupIndex, inputIndex, size, noCacheWrite) );
		return apply(groupInputs[0].size, [&](size_t index) -> SourceImage {
			array<ImageF> inputs;
			for(size_t inputIndex: range(operation.inputs())) inputs.append( share(groupInputs[inputIndex][index]) );
			array<ImageF> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( size?:this->size(groupIndex) );
			operation.apply(outputs, inputs);
			return move(outputs[0]);
		});
	}
};
