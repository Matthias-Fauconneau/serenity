#include "edit.h"
#include "window.h"

struct Test {
	TextEdit text {color(toUCS4("Blue"_), blue)+color(toUCS4("Green"_), green)+color(toUCS4("Red"_), red)};
	unique<Window> window = ::window(&text, 512);
} test;
