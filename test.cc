#include "edit.h"
#include "window.h"

struct Test {
	TextEdit text {"Hello World!\nshort.\nLONG LINE !"_};
	unique<Window> window = ::window(&text, 512);
} test;
