#include "pdf.h"
#include "variant.h"
#include "matrix.h"
#include "deflate.h"

#if RASTERIZED_PDF
buffer<byte> toPDF(const ref<Image>& images) {
    array<unique<Object>> objects;
    auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"_; };

    objects.append(); // Null object

    Object& root = objects.append();
    root.insert("Type"_, String("/Catalog"_));

    Object& pages = objects.append();
    pages.insert("Type"_, String("/Pages"_));
    pages.insert("Kids"_, array<Variant>());
    pages.insert("Count"_, 0);

    for(const Image& image: images) {
        Object& page = objects.append();
        page.insert("Parent"_, ref(pages));
        page.insert("Type"_, String("/Page"_));
        {Dict resources;
            {Dict xObjects;
                {Object& xImage = objects.append();
                    xImage.insert("Subtype"_, String("/Image"_));
                    xImage.insert("Width"_, dec(image.width));
                    xImage.insert("Height"_, dec(image.height));
                    xImage.insert("ColorSpace"_, String("/DeviceRGB"_));
                    xImage.insert("BitsPerComponent"_, String("8"_));
                    buffer<byte> rgb3 (image.height * image.width * 3);
                    assert_(image.buffer.size == image.height * image.width);
                    for(uint index: range(image.buffer.size)) {
                        rgb3[index*3+0] = image.buffer[index].r;
                        rgb3[index*3+1] = image.buffer[index].g;
                        rgb3[index*3+2] = image.buffer[index].b;
                    }
                    xImage.insert("Filter"_,"/FlateDecode"_);
                    xImage = deflate(move(rgb3));
                    xObjects.insert("Image"_, ref(xImage));}
                resources.insert("XObject"_, move(xObjects));}
            page.insert("Resources"_, move(resources));}
        {array<Variant> mediaBox; mediaBox.append( 0 ).append( 0 ).append( image.width ).append( image.height );
            page.insert("MediaBox"_, move(mediaBox));}
        {Object& contents = objects.append();
            mat3 im (vec3(1.f/image.width,0,0), vec3(0,1.f/image.height,0), vec3(0,0,1));
            mat3 m = im.inverse();
            contents = dec<float>({m(0,0),m(0,1),m(1,0),m(1,1),m(0,2),m(1,2)})+" cm /Image Do"_;
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
    return move(file);
}
#else
buffer<byte> toPDF(/*ref<string> texts*/) {
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
                                cidFont.insert("FontDescriptor"_, ref(fontDescriptor));}
                            cidFont.insert("CIDToGIDMap"_,"/Identity"_);
                            descendantFonts << move(cidFont);}
                        xFont.insert("DescendantFonts"_, /*ref*/ move(descendantFonts));}
                    //ToUnicode
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
    return move(file);
}
#endif
