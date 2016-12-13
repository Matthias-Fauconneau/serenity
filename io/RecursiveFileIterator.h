#pragma once
#include "FileIterator.h"
#include <stack>

class RecursiveFileIterator
{
    std::stack<FileIterator> _iterators;

public:
    RecursiveFileIterator() = default;
    RecursiveFileIterator(const Path &p);

    bool operator==(const RecursiveFileIterator &o) const;
    bool operator!=(const RecursiveFileIterator &o) const;

    RecursiveFileIterator &operator++();
    RecursiveFileIterator  operator++(int);

    Path &operator*();
    const Path &operator*() const;
};
