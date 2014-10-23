#pragma once
#include "source.h"
#include "operator.h"

/// Provides outputs of an \a operation on a \a source
struct GenericImageOperation : virtual GenericImageSource {
	GenericImageSource& source;
	Operator& operation;
	Folder cacheFolder {operation.name(), source.folder(), true};
	GenericImageOperation(GenericImageSource& source, Operator& operation) : source(source), operation(operation) {}

	const Folder& folder() const override { return cacheFolder; }
	String name() const override { return str(source.name(), operation.name()); }
	size_t count(size_t need = 0) override { return source.count(need); }
    int2 maximumSize() const override { return source.maximumSize(); }
	String elementName(size_t index) const override { return source.elementName(index); }
	int64 time(size_t index) override { return max(source.time(index), operation.time()); }
	int2 size(size_t index) const override { return source.size(index); }
};

struct ImageOperation : GenericImageOperation, ImageSource {
	ImageSource& source;
	ImageOperator& operation;
	ImageOperation(ImageSource& source, ImageOperator& operation)
		: GenericImageOperation(source, operation), source(source), operation(operation) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "GenericImageOperation");
		return source.outputs();
	}
	/// Returns processed linear image
	SourceImage image(size_t imageIndex, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return GenericImageSource::toString()+'['+str(outputs())+']'; }
};

generic struct ImageOperationT : T, ImageOperation {
	ImageOperationT(ImageSource& source) : ImageOperation(source, *this) {}
};

struct SRGB : GenericImageOperator, OperatorT<SRGB> {
	size_t inputs() const override { return 1; }
	size_t outputs() const override { return 1; }
	string name() const override { return  "[sRGB]"; }
};
struct sRGBOperation : GenericImageOperation, ImageRGBSource, SRGB {
	ImageSource& source;
	sRGBOperation(ImageSource& source) : GenericImageOperation(source, *this), source(source) {}

	/// Returns processed sRGB image
	virtual SourceImageRGB image(size_t index, int2 size, bool noCacheWrite) override;
};

/// Evaluates an image for each group
struct ImageGroupFold : GenericImageOperation, ImageSource {
	ImageGroupSource& source;
	ImageOperator& operation;
	ImageGroupFold(ImageGroupSource& source, ImageOperator& operation)
		: GenericImageOperation(source, operation), source(source), operation(operation) {}

	size_t outputs() const override {
		if((operation.inputs() == 0 && operation.outputs()>1) || source.outputs() == operation.inputs()) return operation.outputs();
		assert_(operation.inputs() == 0 && operation.outputs() == 1,
				operation.name(), operation.inputs(), operation.outputs(), source.name(), "ImageGroupFold");
		return source.outputs();
	}
	SourceImage image(size_t groupIndex, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return str(source.toString(), operation.name())+'['+str(outputs())+']'; }
};

generic struct ImageGroupFoldT : T, ImageGroupFold {
	ImageGroupFoldT(ImageGroupSource& source) : ImageGroupFold(source, *this) {}
};

/// Returns image groups from an image source grouped by a group source
struct GroupImageOperation : ImageGroupSource {
	ImageSource& source;
	GroupSource& groups;
	GroupImageOperation(ImageSource& source, GroupSource& groups) : source(source), groups(groups) {}
	size_t count(size_t need=0) override { return groups.count(need); }
	String name() const override { return source.name(); }
	const Folder& folder() const override { return source.folder(); }
	int2 maximumSize() const override { return source.maximumSize(); }
	int64 time(size_t groupIndex) override { return max(apply(groups(groupIndex), [this](size_t index) { return source.time(index); })); }
	String elementName(size_t groupIndex) const override;
	int2 size(size_t groupIndex) const override;

	size_t outputs() const override { return source.outputs(); }
	size_t groupSize(size_t groupIndex) const { return groups(groupIndex).size; }
	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override;
};

struct GenericImageGroupOperation : GenericImageOperation, virtual GenericImageGroupSource {
	ImageGroupSource& source;
	GenericImageOperator& operation;
	GenericImageGroupOperation(ImageGroupSource& source, GenericImageOperator& operation)
		: GenericImageOperation(source, operation), source(source), operation(operation) {}

	size_t outputs() const override {
		if(source.outputs() == operation.inputs()) return operation.outputs();
		return (operation.inputs() == 0 && operation.outputs() == 0) ? source.outputs() : operation.outputs();
	}
	size_t groupSize(size_t groupIndex) const { return source.groupSize(groupIndex); }
	String toString() const override { return str(source.toString(), operation.name())+'['+str(outputs())+']'; }
};

/// Evaluates an operation on every image of an image group
struct ImageGroupOperation : GenericImageGroupOperation, ImageGroupSource {
	ImageOperator& operation;
	ImageGroupOperation(ImageGroupSource& source, ImageOperator& operation)
		: GenericImageGroupOperation(source, operation), operation(operation) {}

	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override;
};

generic struct ImageGroupOperationT : T, ImageGroupOperation {
	ImageGroupOperationT(ImageGroupSource& source) : ImageGroupOperation(source, *this) {}
};

struct sRGBGroupOperation : GenericImageGroupOperation, ImageRGBGroupSource, SRGB {
	sRGBGroupOperation(ImageGroupSource& source) : GenericImageGroupOperation(source, *this) {}
	array<SourceImageRGB> images(size_t groupIndex, int2 size = 0, bool noCacheWrite = false) override;
};

/// Operates on two image sources
struct BinaryGenericImageOperation : virtual GenericImageSource {
	GenericImageSource& A;
	GenericImageSource& B;
	ImageOperator& operation;
	Folder cacheFolder {A.name()+B.name(), A.folder()/*FIXME: MRCA of A and B*/, true};
	BinaryGenericImageOperation(GenericImageSource& A, GenericImageSource& B, ImageOperator& operation)
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

/// Operates on two image group sources
struct BinaryImageGroupOperation : BinaryGenericImageOperation, ImageGroupSource {
	ImageGroupSource& A;
	ImageGroupSource& B;
	BinaryImageGroupOperation(ImageGroupSource& A, ImageGroupSource& B, ImageOperator& operation)
		: BinaryGenericImageOperation(A, B, operation), A(A), B(B) {}

	size_t outputs() const override {
		//if(A.outputs()+B.outputs() == operation.inputs()) return operation.outputs();
		return B.outputs() * operation.outputs(); // Distributes binary operator on every output of B
	}
	size_t groupSize(size_t groupIndex) const { assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex)); return A.groupSize(groupIndex); }

	array<SourceImage> images(size_t groupIndex, size_t componentIndex, int2 size = 0, bool noCacheWrite = false) override;

	String toString() const override { return BinaryGenericImageOperation::toString()+'['+str(outputs())+']'; }
};

generic struct BinaryImageGroupOperationT : T, BinaryImageGroupOperation {
	BinaryImageGroupOperationT(ImageGroupSource& A, ImageGroupSource& B) : BinaryImageGroupOperation(A, B, *this) {}
};
