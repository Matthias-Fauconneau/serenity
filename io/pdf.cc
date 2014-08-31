#include "pdf.h"
#include "variant.h"

#if RASTERIZED_PDF
#include "matrix.h"
#include "deflate.h"

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
#include "data.h"

buffer<byte> toPDF(int2 pageSize, const ref<Graphics>& pages) {
    array<unique<Object>> objects;
    auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"_; };

    objects.append(); // Null object

    Object& root = objects.append();
    root.insert("Type"_, String("/Catalog"_));

    Object& pdfPages = objects.append();
    pdfPages.insert("Type"_, String("/Pages"_));
    pdfPages.insert("Kids"_, array<Variant>());
    pdfPages.insert("Count"_, 0);

    map<String, String> fonts; // font ID to font ref

    for(const Graphics& page: pages) {
        Object& pdfPage = objects.append();
        pdfPage.insert("Parent"_, ref(pdfPages));
        pdfPage.insert("Type"_, String("/Page"_));
        {array<Variant> mediaBox;
            mediaBox.append( 0 ).append( 0 ).append(pageSize.x).append(pageSize.y);
            pdfPage.insert("MediaBox"_, move(mediaBox));}
        {Object& contents = objects.append();
            String content;
            content << "BT\n"_;
            Dict xFonts;
            vec2 last = 0;
            for(const Glyph& glyph: page.glyphs) { // FIXME: Optimize redundant state changes
                const Font& font = glyph.font;
                if(!xFonts.contains(font.id)) {
                    if(!fonts.contains(font.id)) {
                        Object& xFont = objects.append();
                        xFont.insert("Type"_, String("/Font"_));
                        xFont.insert("Subtype"_, String("/Type0"_));
                        xFont.insert("BaseFont"_, String("/Font"_));
                        xFont.insert("Encoding"_, String("/Identity"_));
                        {array<Variant> descendantFonts;
                            {Dict cidFont;
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
                            xFont.insert("DescendantFonts"_, move(descendantFonts));}
                        //TODO: ToUnicode
                        fonts.insert(copy(font.id), ref(xFont));
                    }
                    xFonts.insert(font.id, fonts.at(font.id));
                }
                content << "/"_+font.id+" "_+dec(glyph.font.size)+" Tf\n"_; // Font size in pixels
                vec2 delta = glyph.origin - last; // Position delta of glyph origin in pixels
                last = glyph.origin;
                content << dec<float>({delta.x, delta.y}) << " Td\n"_;
                uint index = font.index(glyph.code);
                assert_(index < 1<<15);
                content << "("_+raw<uint16>(big16(index))+") Tj\n"_;
            }
            content << "ET\n"_;
            contents = move(content);
            pdfPage.insert("Contents"_, ref(contents));

            {Dict resources;
                resources.insert("Font"_, move(xFonts));
                pdfPage.insert("Resources"_, move(resources));}
        }
        pdfPages.at("Kids"_).list << ref(pdfPage);
        pdfPages.at("Count"_).number++;
    }
    root.insert("Pages"_, ref(pdfPages));

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
