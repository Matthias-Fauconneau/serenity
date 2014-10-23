#pragma once
#include "source.h"
#include "operation.h"

// UnaryGenericImageSource

/// Provides outputs of an \a operation on a \a source
struct UnaryGenericImageSource : virtual GenericImageSource {
	GenericImageSource& source;
	GenericImageOperation& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	UnaryGenericImageSource(GenericImageSource& source, GenericImageOperation& operation) : source(source), operation(operation) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(source.name(), operation.name()); }
	size_t count(size_t need = 0) override { return source.count(need); }
    int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return max(source.time(index), operation.time()); }
	int2 size(size_t index) const override { return source.size(index); }
};

struct UnaryImageSource : UnaryGenericImageSource, ImageSource {
	ImageSource& source;
	ImageOperation& operation;
	UnaryImageSource(ImageSource& source, ImageOperation& operation)
		: UnaryGenericImageSource(source, operation), source(source), operation(operation) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "UnaryGenericImageSource");
		return source.outputs();
	}
	/// Returns processed linear image
	SourceImage image(size_t imageIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

generic struct UnaryImageSourceT : T, UnaryImageSource {
	UnaryImageSourceT(ImageSource& source) : UnaryImageSource(source, *this) {}
};

struct SRGB : GenericImageOperation, OperationT<SRGB> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	string name() const override { return  "[sRGB]"; }
};
struct sRGBImageSource : UnaryGenericImageSource, ImageRGBSource, SRGB {
	ImageSource& source;
	sRGBImageSource(ImageSource& source) : UnaryGenericImageSource(source, *this), source(source) {}

	/// Returns processed sRGB image
	virtual SourceImageRGB image(size_t index, int2 size, bool noCacheWrite) override;
};

/// Evaluates an image for each group
struct UnaryGroupImageSource : UnaryGenericImageSource, ImageSource {
	ImageGroupSource& source;
	ImageOperation& operation;
	UnaryGroupImageSource(ImageGroupSource& source, ImageOperation& operation)
		: UnaryGenericImageSource(source, operation), source(source), operation(operation) {}

	size_t outputs() const override {
		if((operation.inputs() == 0 && operation.outputs()>1) || source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "UnaryGroupImageSource");
		return source.outputs();
	}
	SourceImage image(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return str(source.toString(), operation.name())+'['+str(outputs())+']'; }
};

generic struct UnaryGroupImageSourceT : T, UnaryGroupImageSource {
	UnaryGroupImageSourceT(ImageGroupSource& source) : UnaryGroupImageSource(source, *this) {}
};

// ProcessedImageGroupSource

/// Returns image groups from an image source grouped by a group source
struct GroupImageGroupSource : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	GroupImageGroupSource(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
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

// UnaryImageGroupSource

struct GenericProcessedImageGroupSource : UnaryGenericImageSource, virtual GenericImageGroupSource {
	ImageGroupSource& source;
	GenericProcessedImageGroupSource(ImageGroupSource& source, GenericImageOperation& operation)
		: UnaryGenericImageSource(source, operation), source(source) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 0,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "UnaryImageGroupSource");
		return source.outputs();
	}
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }
};

/// Evaluates an operation on every image of an image group
struct UnaryImageGroupSource : GenericProcessedImageGroupSource, ImageGroupSource {
	ImageOperation& operation;
	UnaryImageGroupSource(ImageGroupSource& source, ImageOperation& operation)
		: GenericProcessedImageGroupSource(source, operation), operation(operation) {}

	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;
};

generic struct UnaryImageGroupSourceT : T, UnaryImageGroupSource {
	UnaryImageGroupSourceT(ImageGroupSource& source) : UnaryImageGroupSource(source, *this) {}
};

struct sRGBImageGroupSource : GenericProcessedImageGroupSource, ImageRGBGroupSource, SRGB {
	sRGBImageGroupSource(ImageGroupSource& source) : GenericProcessedImageGroupSource(source, *this) {}
	array<SourceImageRGB> images(size_t groupIndex, int2 size = 0, bool noCacheWrite = false) override;
};


// BinaryImageSource

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

// BinaryImageGroupSource

/// Evaluates an operation on every image of an image group
struct BinaryImageGroupSource : BinaryGenericImageSource, ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	BinaryImageGroupSource(ImageGroupSource& A, ImageGroupSource& B, ImageOperation& operation)
		: BinaryGenericImageSource(A, B, operation), A(A), B(B) {}

	size_t outputs() const override {
		//if(A.outputs()+B.outputs() == operation.inputs()) return operation.outputs();
		return B.outputs() * operation.outputs(); // Distributes binary operator on every output of B
	}
	size_t groupSize(size_t groupIndex) const { assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex)); return A.groupSize(groupIndex); }

	array<SourceImage> images(size_t groupIndex, size_t outputIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return BinaryGenericImageSource::toString()+'['+str(outputs())+']'; }
};

generic struct BinaryImageGroupSourceT : T, BinaryImageGroupSource {
	BinaryImageGroupSourceT(ImageGroupSource& A, ImageGroupSource& B) : BinaryImageGroupSource(A, B, *this) {}
};
