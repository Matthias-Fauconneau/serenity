#pragma once
/// view.h Abstract interface to view results
#include "result.h"

struct View {
    virtual bool view(string metadata, string name, const buffer<byte>& data) abstract;
    virtual ~View() {}
};
