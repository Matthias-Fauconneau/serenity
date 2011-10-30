#pragma once
#include "common.h"

struct Recognizer {
    virtual void onGlyph(int, vec2, float,const string&, int) = 0;
    virtual void onPath(const array<vec2>&) = 0;
};

struct Document {
    virtual ~Document() {}
    virtual void open(const string& path, Recognizer* recognizer) =0;
    virtual void resize(int width, int height) =0;
    virtual void render(float scroll) =0;
};
