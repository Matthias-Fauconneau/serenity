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
    String name(size_t index) const override { return source.name(index); }
    int64 time(size_t index) const override { return max(operation.time(), source.time(index)); }
    const map<String, String>& properties(size_t index) const override { return source.properties(index); }
    int2 size(size_t index) const override { return source.size(index); }

    /// Returns processed sRGB image
    virtual SourceImageRGB image(size_t index, int2 size) const override;
};
