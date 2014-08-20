#include "thread.h"
#include "data.h"
#include "font.h"
#include "text.h"
#include "layout.h"
#include "interface.h"
#include "png.h"
#include "variant.h"
#include "matrix.h"
#include "pdf.h"
#include "widget.h"
#include "window.h"

struct PDFTest {
    /*PDF pdf;
    Window window {&pdf,int2(0,0),"PDFTest"_};*/
    PDFTest() {
        writeFile("test.pdf"_,textToPDF(/*{"Hello World !"_}*/));
        /*pdf.open(readFile("test.pdf"_,home()));
        window.background = White;
        window.actions[Escape]=[]{exit();};
        window.show();*/
    }
} test;
