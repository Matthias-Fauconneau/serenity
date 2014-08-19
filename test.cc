#include "thread.h"
#include "variant.h"
#include "image.h"
#include "png.h"
#include "matrix.h"
#include "data.h"

struct PDFWriteTest {
    PDFWriteTest() {
        array<unique<Object>> objects;
        auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"_; };

        objects.append(); // Null object

        Object& root = objects.append();
        root.insert("Type"_, String("/Catalog"_));

        Object& pages = objects.append();
        pages.insert("Type"_, String("/Pages"_));
        pages.insert("Kids"_, array<Variant>());
        pages.insert("Count"_, 0);

        Folder folder("Rapport.out"_,home());
        const array<String> files = folder.list(Files|Sorted);
        for(uint pageIndex=0; pageIndex<1 /*DEBUG*/; pageIndex++) {
            String path = dec(pageIndex);
            if(!files.contains(path)) break;
            Image image = decodeImage(readFile(path, folder));
            assert_(image, path);

            Object& page = objects.append();
            page.insert("Parent"_, ref(pages));
            page.insert("Type"_, String("/Page"_));
            {Dict resources;
                {Dict xObjects;
                    {Object& xImage = objects.append();
                        //xObject.insert("Type"_, String("/XObject"_));
                        xImage.insert("Subtype"_, String("/Image"_));
                        xImage.insert("Width"_, dec(image.width));
                        xImage.insert("Height"_, dec(image.height));
                        xImage.insert("ColorSpace"_, String("/DeviceRGB"_));
                        xImage.insert("BitsPerComponent"_, String("8"_));
                        xImage/*.data*/ = cast<byte>(copy(image.buffer));
                        assert_(xImage.data.size == image.height * image.width * 4);
                        //xImage.insert("Length"_, xImage.data.size);
                        xObjects.insert("Image"_, ref(xImage));}
                    resources.insert("XObject"_, move(xObjects));}
                page.insert("Resources"_, move(resources));}
            {array<Variant> mediaBox; mediaBox.append( 0 ).append( 0 ).append( image.width ).append( image.height );
                page.insert("MediaBox"_, move(mediaBox));}
            {Object& contents = objects.append();
                mat3 im (vec3(1.f/image.width,0,0), vec3(0,-1.f/image.height,0), vec3(0,1,1));
                mat3 m = im.inverse();
                contents/*.data*/ = dec<float>({m(0,0),m(0,1),m(1,0),m(1,1),m(0,2),m(1,2)})+" cm /Image Do"_;
                page.insert("Contents"_, ref(contents));}
            pages.at("Kids"_).list << ref(page);
            pages.at("Count"_).number++;
        }
        root.insert("Pages"_, ref(pages));

        array<byte> file = String("%PDF-1.7\n"_);
        array<uint> xrefs (objects.size);
        for(uint index: range(1, objects.size)) {
            xrefs << file.size;
            file << dec(index) << " 0 obj\n"_ << str(objects[index]) << "\nendobj\n"_;
        }
        int index = file.size;
        file << "xref\n0 "_ << dec(xrefs.size+1) << "\n0000000000 65535 f\r\n"_;
        for(uint index: xrefs) {
            String entry (20);
            entry << dec(index, 10, '0') << " 00000 n\r\n"_;
            assert(entry.size==20);
            file << entry;
        }
        Dict trailer; trailer.insert("Size"_, int(1+xrefs.size)); assert_(objects); trailer.insert("Root"_, ref(root));
        file << "trailer "_ << str(trailer) << "\n"_;
        file << "startxref\n"_ << dec(index) << "\r\n%%EOF"_;
        writeFile("test.pdf"_, file, home());
        //log(file.slice(244));
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
