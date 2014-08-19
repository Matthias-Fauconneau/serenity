#include "widget.h"
#include "window.h"
#include "time.h"
#include "pdf.h"

struct PDFTest {
    /*Scroll<*/PDF/*>*/ pdf;
    Window window {&pdf/*.area()*/,int2(0,0),"PDFTest"_};
    PDFTest() {
        pdf.open(readFile("0.pdf"_,home()));
        window.background = White;
        window.actions[Escape]=[]{exit();};
        window.show();
    }
} test;
