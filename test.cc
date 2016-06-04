#include "window.h"

struct Test : Widget {
 unique<Window> window = nullptr;
 Test() {
  window = ::window(this);
  window->backgroundColor = nan;
  //window->presentComplete = [this]{ next(); window->render(); };
 }
 vec2 sizeHint(vec2) override { return vec2(512); }
 shared<Graphics> graphics(vec2) override {
  window->target.clear(byte4(0xFF,0,0,0xFF));
  return shared<Graphics>();
 }
} test;
