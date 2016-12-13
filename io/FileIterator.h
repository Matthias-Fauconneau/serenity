#pragma once
#include "FileUtils.h"
#include "Path.h"
#include <memory>

class FileIterator
{
    Path _dir;
    bool _ignoreFiles;
    bool _ignoreDirectories;
    Path _extensionFilter;

    std::shared_ptr<OpenDir> _openDir;

    Path _currentEntry;

public:
    FileIterator();
    FileIterator(const Path &p, bool ignoreFiles, bool ignoreDirectories, const Path &extensionFilter);

    bool operator==(const FileIterator &o) const;
    bool operator!=(const FileIterator &o) const;

    FileIterator &operator++();
    FileIterator  operator++(int);

    Path &operator*();
    const Path &operator*() const;
};
