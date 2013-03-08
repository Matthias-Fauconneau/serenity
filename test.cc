#if 0
#include "window.h"
#include "pdf.h"
#include "interface.h"

struct PDFTest {
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(0,0),"PDFTest"_);
    PDFTest() {
        pdf.open(readFile(".pdf",root()));
        window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Escape).connect(&exit);
    }
} test;
#endif

#if 1
#include "process.h"
#include "widget.h"
#include "display.h"
#include "window.h"
struct VSyncTest : Widget {
    Window window __(this,0,"VSync"_,Window::OpenGL);
    VSyncTest(){ window.localShortcut(Escape).connect(&exit); }
    uint count=0; void render(int2 position, int2 size) {fill(position+Rect(size),((count++)%2)?black:white); window.render(); if(count>100) exit();}
} test;
#endif
