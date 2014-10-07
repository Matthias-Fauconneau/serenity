#include "pdf.h"
#include "variant.h"
#include "data.h"
#include "matrix.h"

typedef map<string,Variant> Dict;

struct Object : Dict {
    buffer<byte> data;

    void operator =(buffer<byte>&& data) {
        this->data = move(data);
        insert("Length"_, this->data.size);
    }
};

inline String str(const Object& o) {
    String s = str((const Dict&)o);
    if(o.data) {
        assert_(o.at("Length"_).integer() == int(o.data.size), (const Dict&)o);
        s << "\nstream\n"_;
        assert_(o.data.size <= 30 || o.value("Filter"_,""_)=="/FlateDecode"_, o.data.size, deflate(o.data).size, o.value("Filter"_,""_));
        s << o.data;
        s << "\nendstream"_;
    }
    return s;
}

buffer<byte> toPDF(int2 pageSize, const ref<Graphics> pages, float px) {
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

    for(const Graphics& graphics: pages) {
        Object& page = objects.append();
        page.insert("Parent"_, ref(pdfPages));
        page.insert("Type"_, String("/Page"_));
        {array<Variant> mediaBox;
            mediaBox.append( 0 ).append( 0 ).append(pageSize.x*px).append(pageSize.y*px);
            page.insert("MediaBox"_, move(mediaBox));}
        //page.insert("UserUnit"_, userUnit); log(userUnit); // Minimum user unit is 1
        {Object& contents = objects.append();
            String content;
            content << "BT\n"_;
            Dict xFonts;
            String fontID; float fontSize=0;
            vec2 last = 0;
            for(const Glyph& glyph: graphics.glyphs) { // FIXME: Optimize redundant state changes
                const Font& font = glyph.font;
                if(font.name != fontID || font.size != fontSize) {
                    if(!xFonts.contains(font.name)) {
                        if(!fonts.contains(font.name)) {
                            Object& xFont = objects.append();
                            xFont.insert("Type"_, String("/Font"_));
                            xFont.insert("Subtype"_, String("/Type0"_));
                            xFont.insert("BaseFont"_, String("/Font"_));
                            xFont.insert("Encoding"_, String("/Identity-H"_));
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
                                            fontFile.insert("Filter"_,"/FlateDecode"_);
                                            fontFile = deflate(font.data);
                                            fontDescriptor.insert("FontFile2"_, ref(fontFile));}
                                        cidFont.insert("FontDescriptor"_, ref(fontDescriptor));}
                                    cidFont.insert("CIDToGIDMap"_,"/Identity"_);
                                    descendantFonts << move(cidFont);}
                                xFont.insert("DescendantFonts"_, move(descendantFonts));}
                            //TODO: ToUnicode
                            fonts.insert(copy(font.name), ref(xFont));
                        }
                        xFonts.insert(font.name, fonts.at(font.name));
                    }
                    content << "/"_+font.name+" "_+dec(glyph.font.size*px)+" Tf\n"_; // Font size in pixels
                }
                uint index = font.index(glyph.code);
                assert_(index < 1<<15);
                vec2 origin = vec2(glyph.origin.x, pageSize.y-glyph.origin.y)*px;
                vec2 relative = origin - last; // Position update of glyph origin in pixels
                last = origin;
                content << str(relative.x,relative.y)+" Td <"_+hex(index,4)+"> Tj\n"_;
            }
            content << "ET\n"_;

            {Dict resources;
                if(graphics.blits) {
                    map<String, Variant> xObjects;
                    for(int index: range(graphics.blits.size)) {
                        const Blit& blit = graphics.blits[index];
                        const auto& image = blit.image;
                        String id = "Image"_+str(pdfPages.at("Count"_).number)+"_"_+str(index);
                        {Object& xImage = objects.append();
                            xImage.insert("Subtype"_, String("/Image"_));
                            xImage.insert("Width"_, dec(image.width));
                            xImage.insert("Height"_, dec(image.height));
                            xImage.insert("ColorSpace"_, String("/DeviceRGB"_));
                            xImage.insert("BitsPerComponent"_, String("8"_));
                            buffer<byte3> rgb3 (image.height * image.width); static_assert(sizeof(byte3)==3, "");
                            for(uint y: range(image.height)) for(uint x: range(image.width)) rgb3[y*image.width+x] = image.data[y*image.stride+x].rgb();
                            xImage.insert("Filter"_,"/FlateDecode"_);
                            xImage = deflate(cast<byte>(rgb3));
                            xObjects.insert(copy(id), ref(xImage));
                        }
                        assert_(image.width && image.height, image.size);
                        content << "q "_+str(blit.size.x*px,0,0,blit.size.y*px, blit.origin.x*px, (pageSize.y-blit.origin.y-blit.size.y)*px)+" cm /"_+id+" Do Q\n"_;
                    }
                    resources.insert("XObject"_, move(xObjects));
                }
                resources.insert("Font"_, move(xFonts));
                page.insert("Resources"_, move(resources));
            }

            for(auto& line: graphics.lines) content << str(line.a.x*px, (pageSize.y-line.a.y)*px)+" m "_+str(line.b.x*px, (pageSize.y-line.b.y)*px)+" l S\n"_;

            contents.insert("Filter"_,"/FlateDecode"_);
            contents = deflate(content);
            page.insert("Contents"_, ref(contents));
        }
        pdfPages.at("Kids"_).list << ref(page);
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
