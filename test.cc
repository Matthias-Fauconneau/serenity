#include "thread.h"
#include "variant.h"

String str(const Dict& dict) {
    String s;
    s << "<< "_;
    for(const const_pair<string,Variant>& entry: dict) s << "/"_+entry.key+" "_<<str(entry.value)<<" "_;
    s << ">>"_;
    return s;
}
String str(const array<Variant>& array) {
    String s;
    s << "["_;
    for(const Variant& element: array) s << element << " "_;
    s << "]"_;
    return s;
}

struct PDFWriteTest {
    PDFWriteTest() {
        Dict catalog;
        catalog.insert("Type"_, String("/Catalog"_));
        catalog.insert("Pages"_, String("2 0 R"_));
        Dict pagesDict;
        pagesDict.insert("Type"_, String("/Pages"_));
        array<Dict> pages;
        array<Variant> pageReferences; for(int index: range(pages.size)) pageReferences.append(dec(2+index)+" 0 R"_);
        pagesDict.insert("Kids"_, str(pageReferences));
        pagesDict.insert("Count"_, str(pages.size));
        array<String> objects;
        objects << str(catalog);
        objects << str(pagesDict);
        array<byte> file = String("%PDF-1.7\n"_);
        array<uint> xrefs (objects.size);
        for(uint index: range(objects.size)) {
            xrefs << file.size;
            file << dec(index+1) << " 0 obj\n"_ << objects[index] << "\nendobj\n"_;
        }
        int index = file.size;
        file << "xref\n0 "_ << dec(xrefs.size+1) << "\n0000000000 65535 f\r\n"_;
        for(uint index: xrefs) {
            String entry (20);
            entry << dec(index, 10, '0') << " 00000 n\r\n"_;
            assert(entry.size==20);
            file << entry;
        }
        Dict trailer; trailer.insert("Size"_, int(1+xrefs.size)); assert_(objects); trailer.insert("Root"_, String("1 0 R"_));
        file << "trailer "_ << str(trailer) << "\n"_;
        file << "startxref\n"_ << dec(index) << "\r\n%%EOF"_;
        log(file);
        writeFile("test.pdf"_, file, home());
    }
} test;

#if 0
#include "pdf.h"
#include "widget.h"
#include "window.h"
struct PDFReadTest {
    PDF pdf;
    Window window {&pdf,int2(0,0),"PDFReadTest"_};
    PDFReadTest() {
        pdf.open(readFile("0.pdf"_,home()));
        window.background = White;
        window.actions[Escape]=[]{exit();};
        window.show();
    }
} test;
#endif
