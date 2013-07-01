#pragma once
/// view.h Abstract interface to view results
#include "result.h"
#include "widget.h"

struct View : virtual Widget {
    virtual bool view(const string& metadata, const string& name, const buffer<byte>& data) abstract;
    virtual ~View() {}
};
