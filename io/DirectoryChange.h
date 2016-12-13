#pragma once
#include "FileUtils.h"
#include "Path.h"

class DirectoryChange
{
    Path _previousDir;

public:
    DirectoryChange(const Path &path)
    {
        if (!path.empty()) {
            _previousDir = FileUtils::getCurrentDir();
            FileUtils::changeCurrentDir(path);
        }
    }

    ~DirectoryChange()
    {
        if (!_previousDir.empty())
            FileUtils::changeCurrentDir(_previousDir);
    }
};
