#include "window.h"
#include "pdf.h"
#include "interface.h"

struct PDFTest {
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(0,0),"PDFTest"_);
    PDFTest() {
        //pdf.open(readFile("Sheets/Battlestar Sonatica.pdf",root()));
        pdf.open(readFile("Sheets/Evenstar.pdf",root()));
        window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Escape).connect(&exit);
    }
} test;
