#include "thread.h"
#include "data.h"
#include "font.h"
#include "text.h"
#include "layout.h"
#include "interface.h"
#include "png.h"
#include "variant.h"
#include "matrix.h"

buffer<byte> textToPDF(ref<string> texts) {
    array<unique<Object>> objects;
    auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"_; };

    objects.append(); // Null object

    Object& root = objects.append();
    root.insert("Type"_, String("/Catalog"_));

    Object& pages = objects.append();
    pages.insert("Type"_, String("/Pages"_));
    pages.insert("Kids"_, array<Variant>());
    pages.insert("Count"_, 0);

    for(const string& text: texts) {
        Object& page = objects.append();
        page.insert("Parent"_, ref(pages));
        page.insert("Type"_, String("/Page"_));
        {Dict resources;
            {Dict xFonts;
                {Object& xFont = objects.append();
                    xFont.insert("Type"_, String("/Font"_));
                    xFont.insert("Subtype"_, String("/Type0"_));
                    xFont.insert("BaseFont"_, String("/Font"_));
                    xFont.insert("Encoding"_, String("/Identity-H"_));
                    {array<Variant> descendantFonts;
                        {Dict cidFont; //Object& cidFont = objects.append();
                            cidFont.insert("Type"_, String("/Font"_));
                            cidFont.insert("Subtype"_, String("/CIDFontType2"_));
                            cidFont.insert("BaseFont"_, String("/Font"_));
                            {Dict cidSystemInfo;
                                cidSystemInfo.insert("Registry"_,"/R"_);
                                cidSystemInfo.insert("Ordering"_,"/O"_);
                                cidSystemInfo.insert("Supplement"_, 0);
                                cidFont.insert("CIDSystemInfo"_, move(cidSystemInfo));}
                            {Object& fontDescriptor = objects.append();
                                fontDescriptor.insert("Type"_,"/FontDescriptor"_);
                                fontDescriptor.insert("FontName"_,"/Font"_);
                                fontDescriptor.insert("Flags"_, 0); // FIXME
                                fontDescriptor.insert("FontBBox"_, "[0 0 0 0]"_); // FIXME
                                fontDescriptor.insert("ItalicAngle"_, "0"_);
                                fontDescriptor.insert("Ascent"_, "0"_); // FIXME
                                fontDescriptor.insert("Descent"_, "0"_); // Negative FIXME
                                fontDescriptor.insert("StemV"_, "0"_); // FIXME
                                {Object& fontFile = objects.append();
                                    fontFile = readFile(findFont("DejaVuSans"_), fontFolder());
                                    fontDescriptor.insert("FontFile2"_, ref(fontFile));}
                            cidFont.insert("FontDescriptor"_, ref(fontDescriptor));}
                            cidFont.insert("CIDToGIDMap"_,"/Identity"_);
                            descendantFonts << move(cidFont);}
                        xFont.insert("DescendantFonts"_, /*ref*/ move(descendantFonts));}
                    //ToUnicode
                    xFonts.insert("Font"_, ref(xFont));}
                resources.insert("Font"_, move(xFonts));}
            page.insert("Resources"_, move(resources));}
        int width=1050, height=1485;
        {array<Variant> mediaBox; mediaBox.append( 0 ).append( 0 ).append( width ).append( height );
            page.insert("MediaBox"_, move(mediaBox));}
        {Object& contents = objects.append();
            mat3 im (vec3(1.f/width,0,0), vec3(0,1.f/height,0), vec3(0,0,1));
            mat3 m = im.inverse();
            String content;
            content << dec<float>({m(0,0),m(0,1),m(1,0),m(1,1),m(0,2),m(1,2)})+" cm\n"_;
            content << "BT\n"_;
            content << "/Font 12 Tf\n"_; // Font size in pixels
            vec2 delta (width/2, height/2); // Position delta of glyph origin in pixels
            content << dec<float>({delta.x, delta.y}) << " Td\n"_;
            content << "("_+cast<byte>(toUCS2(text))+") Tj\n"_;
            content << "ET\n"_;
            contents = move(content);
            page.insert("Contents"_, ref(contents));}
        pages.at("Kids"_).list << ref(page);
        pages.at("Count"_).number++;
        //log(pages.at("Count"_));
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
    return move(file);
}

struct PDFTest {
    PDFTest() {
        writeFile("test.pdf"_,textToPDF({"Hello World !"_}));
    }
} test;
