#include "window.h"
#include "pdf.h"
#include "interface.h"

struct Book {
    string file;
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(0,0),"Book"_);
    Book() {
        if(arguments()) file=string(arguments().first());
        else if(existsFile("Books/.last"_)) {
            string mark = readFile("Books/.last"_);
            ref<byte> last = section(mark,0);
            if(existsFile(last)) {
                file = string(last);
                pdf.delta.y = toInteger(section(mark,0,1,2));
            }
        }
        pdf.open(readFile(file,root()));
        window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(UpArrow).connect(this,&Book::previous);
        window.localShortcut(LeftArrow).connect(this,&Book::previous);
        window.localShortcut(RightArrow).connect(this,&Book::next);
        window.localShortcut(DownArrow).connect(this,&Book::next);
        window.localShortcut(Power).connect(this,&Book::next);
    }
    void previous() { pdf.delta.y += window.size.y/2; window.render(); save(); }
    void next() { pdf.delta.y -= window.size.y/2; window.render(); save(); }
    void save() { writeFile("Books/.last"_,string(file+"\0"_+dec(pdf.delta.y))); }
} application;
