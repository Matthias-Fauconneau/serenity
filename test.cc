#include "thread.h"
#include "variant.h"
#include "image.h"
#include "png.h"

struct PDFWriteTest {
    PDFWriteTest() {
        constexpr string catalog = "1 0 R"_;
        constexpr string pagesRoot = "2 0 R"_;

        array<Dict> pages;
        Folder folder("Rapport.out"_,home());
        array<String> files = folder.list(Files|Sorted);
        for(;;) {
            String path = dec(pages.size);
            if(!files.contains(path)) { log(path, files); break; }
            Image image = decodeImage(readFile(path, folder));
            assert_(image, path);
            Dict page;
            page.insert("Type"_, String("/Page"_));
            page.insert("Parent"_, String(pagesRoot));
            page.insert("Resources"_, Dict());
            array<Variant> mediaBox; mediaBox.append( 0 ).append( 0 ).append( image.width ).append( image.height );
            page.insert("MediaBox"_, move(mediaBox));
            pages << move(page);
        }

        array<String> objects;

        {Dict catalog;
            catalog.insert("Type"_, String("/Catalog"_));
            catalog.insert("Pages"_, String(pagesRoot));
            objects << str(catalog);}

        {Dict pagesDict;
            pagesDict.insert("Type"_, String("/Pages"_));
            array<Variant> pageReferences; for(int index: range(pages.size)) pageReferences.append(dec(3+index)+" 0 R"_);
            pagesDict.insert("Kids"_, str(pageReferences));
            pagesDict.insert("Count"_, str(pages.size));
            objects << str(pagesDict);
        }

        for(const Dict& page: pages) objects << str(page);

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
        Dict trailer; trailer.insert("Size"_, int(1+xrefs.size)); assert_(objects); trailer.insert("Root"_, String(catalog));
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
