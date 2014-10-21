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

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "ProcessedGenericSource");
		return source.outputs();
	}
	/// Returns processed linear image
	SourceImage image(size_t imageIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
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
	ProcessedGroupImageSource(ImageGroupSource& source, ImageOperation& operation)
		: ProcessedGenericSource(source, operation), source(source) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "ProcessedGroupImageSource");
		return source.outputs();
	}
	SourceImage image(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return str(source.toString(), operation.name())+'['+str(outputs())+']'; }
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

// ProcessedGroupImageGroupSource (Unary)

/// Evaluates an operation on every image of an image group
struct ProcessedGroupImageGroupSource : ProcessedGenericSource, ImageGroupSource {
	ImageGroupSource& source;
	Folder cacheFolder;
	ProcessedGroupImageGroupSource(ImageGroupSource& source, ImageOperation& operation)
		: ProcessedGenericSource(source, operation), source(source) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 0,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "ProcessedGroupImageGroupSource");
		return source.outputs();
	}
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }
	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return str(source.toString(), operation.name())+'['+str(outputs())+']'; }
};

generic struct ProcessedGroupImageGroupSourceT : T, ProcessedGroupImageGroupSource {
	ProcessedGroupImageGroupSourceT(ImageGroupSource& source) : ProcessedGroupImageGroupSource(source, *this) {}
};

// BinaryGroupImageGroupSource

struct BinaryGenericImageSource : virtual GenericImageSource {
	GenericImageSource& A;
	GenericImageSource& B;
	ImageOperation& operation;
	Folder cacheFolder {A.name()+B.name(), A.folder()/*FIXME: MRCA of A and B*/, true};
	BinaryGenericImageSource(GenericImageSource& A, GenericImageSource& B, ImageOperation& operation)
		: A(A), B(B), operation(operation) {}
	size_t count(size_t need=0) override { assert_(A.count(need) == B.count(need)); return A.count(need); }
	String name() const override { return "("+A.name()+" | "+B.name()+")"; }
	const Folder& folder() const override { return cacheFolder; }
	int2 maximumSize() const override { assert_(A.maximumSize() == B.maximumSize()); return A.maximumSize(); }
	int64 time(size_t index) override { return max(max(A.time(index), B.time(index)), operation.time()); }
	virtual String elementName(size_t index) const override {
		assert_(A.elementName(index) == B.elementName(index)); return A.elementName(index);
	}
	int2 size(size_t index) const override { assert_(A.size(index) == B.size(index)); return A.size(index); }

	String toString() const override { return "("+A.toString()+" | "+B.toString()+")"; }
};

/// Evaluates an operation on every image of an image group
struct BinaryGroupImageGroupSource : BinaryGenericImageSource, ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	BinaryGroupImageGroupSource(ImageGroupSource& A, ImageGroupSource& B, ImageOperation& operation)
		: BinaryGenericImageSource(A, B, operation), A(A), B(B) {}

	size_t outputs() const override {
		//if(A.outputs()+B.outputs() == operation.inputs()) return operation.outputs();
		return B.outputs() * operation.outputs(); // Distributes binary operator on every output of B
	}
	size_t groupSize(size_t groupIndex) const { assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex)); return A.groupSize(groupIndex); }

	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return BinaryGenericImageSource::toString()+'['+str(outputs())+']'; }
};

generic struct BinaryGroupImageGroupSourceT : T, BinaryGroupImageGroupSource {
	BinaryGroupImageGroupSourceT(ImageGroupSource& A, ImageGroupSource& B) : BinaryGroupImageGroupSource(A, B, *this) {}
};
