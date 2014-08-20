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

inline String dec(int n) { return dec(int64(n)); }
inline String dec(uint n) { return dec(int64(n)); }
inline String dec(size_t n) { return dec(int64(n)); }
inline String dec(const float& n) { return n==round(n)?dec(int(n)):ftoa(n); }

buffer<byte> textToPDF(/*ref<string> texts*/) {
    array<unique<Object>> objects;
    auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"_; };

    objects.append(); // Null object

    Object& root = objects.append();
    root.insert("Type"_, String("/Catalog"_));

    Object& pages = objects.append();
    pages.insert("Type"_, String("/Pages"_));
    pages.insert("Kids"_, array<Variant>());
    pages.insert("Count"_, 0);

    /*for(const string& unused text: texts)*/ {
        Object& page = objects.append();
        page.insert("Parent"_, ref(pages));
        page.insert("Type"_, String("/Page"_));
        float size = 12;
        Font font(readFile(findFont("DejaVuSans"_), fontFolder()), size);
        {Dict resources;
            {Dict xFonts;
                {Object& xFont = objects.append();
                    xFont.insert("Type"_, String("/Font"_));
                    xFont.insert("Subtype"_, String("/Type0"_));
                    xFont.insert("BaseFont"_, String("/Font"_));
                    xFont.insert("Encoding"_, String("/Identity"_));
                    {array<Variant> descendantFonts;
                        {Dict cidFont; //Object& cidFont = objects.append();
                            cidFont.insert("Type"_, String("/Font"_));
                            cidFont.insert("Subtype"_, String("/CIDFontType2"_));
                            cidFont.insert("BaseFont"_, String("/Font"_));
                            {Dict cidSystemInfo;
                                cidSystemInfo.insert("Registry"_,"(Adobe)"_);
                                cidSystemInfo.insert("Ordering"_,"(Identity)"_);
                                cidSystemInfo.insert("Supplement"_, 0);
                                cidFont.insert("CIDSystemInfo"_, move(cidSystemInfo));}
                            {Object& fontDescriptor = objects.append();
                                fontDescriptor.insert("Type"_,"/FontDescriptor"_);
                                fontDescriptor.insert("FontName"_,"/Font"_);
                                fontDescriptor.insert("Flags"_, 1<<3 /*Symbolic*/);
                                {array<Variant> fontBBox;
                                    fontBBox.append(dec(font.bboxMin .x)).append(dec(font.bboxMin .y))
                                                  .append(dec(font.bboxMax.x)).append(dec(font.bboxMax.y));
                                    fontDescriptor.insert("FontBBox"_, move(fontBBox));}
                                fontDescriptor.insert("ItalicAngle"_, 0);
                                fontDescriptor.insert("Ascent"_, dec(font.ascender));
                                fontDescriptor.insert("Descent"_, dec(font.descender));
                                fontDescriptor.insert("StemV"_, 1);
                                {Object& fontFile = objects.append();
                                    fontFile = copy(font.data);
                                    fontDescriptor.insert("FontFile2"_, ref(fontFile));}
                                log(fontDescriptor);
                                cidFont.insert("FontDescriptor"_, ref(fontDescriptor));}
                            cidFont.insert("CIDToGIDMap"_,"/Identity"_);
                            descendantFonts << move(cidFont);}
                        xFont.insert("DescendantFonts"_, /*ref*/ move(descendantFonts));}
                    //ToUnicode
                    log(xFont);
                    xFonts.insert("Font"_, ref(xFont));}
                resources.insert("Font"_, move(xFonts));}
            page.insert("Resources"_, move(resources));}
        int width=1050, height=1485;
        {array<Variant> mediaBox;
            mediaBox.append( 0 ).append( 0 ).append( width ).append( height );
            page.insert("MediaBox"_, move(mediaBox));}
        {Object& contents = objects.append();
            String content;
            content << "BT\n"_;
            content << "/Font "_+dec(size)+" Tf\n"_; // Font size in pixels
            vec2 delta (width/2, height/2); // Position delta of glyph origin in pixels
            content << dec<float>({delta.x, delta.y}) << " Td\n"_;
            //content << "("_+cast<byte>(toUCS2(text))+") Tj\n"_;
            //uint index = font.index(toUCS2(text)[0]);
            uint index = font.index('H');
            //assert_(index < 1<<8);
            assert_(index < 1<<15);
            content << "("_+raw<uint16>(big16(index))+") Tj\n"_;
            content << "ET\n"_;
            contents = move(content);
            log(contents);
            page.insert("Contents"_, ref(contents));}
        log(page);
        pages.at("Kids"_).list << ref(page);
        pages.at("Count"_).number++;
        //log(pages.at("Count"_));
    }
    root.insert("Pages"_, ref(pages));
    log(root);

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
    log(xrefs);
    Dict trailer; trailer.insert("Size"_, int(1+xrefs.size)); assert_(objects); trailer.insert("Root"_, ref(root));
    log(trailer);
    file << "trailer "_ << str(trailer) << "\n"_;
    file << "startxref\n"_ << dec(index) << "\r\n%%EOF"_;
    return move(file);
}

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
