#pragma once
/// view.h Abstract interface to view results
#include "result.h"

struct View {
    virtual bool view(shared<Result>&& result) abstract;
    virtual ~View() {}
};
