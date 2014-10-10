#include "pdf.h"
#include "variant.h"
#include "data.h"
#include "matrix.h"

generic String str(const map<T,Variant>& dict) {
    String s;
    s.append("<<"); for(auto entry: dict) s.append( '/'+entry.key+' '+str(entry.value)+' ' ); s.append(">>");
    return s;
}

typedef map<string,Variant> Dict;

struct Object : Dict {
    buffer<byte> data;

    void operator =(buffer<byte>&& data) {
        this->data = move(data);
        insert("Length", this->data.size);
    }
};

inline String str(const Object& o) {
    String s = str((const Dict&)o);
    if(o.data) {
        assert_(o.at("Length").integer() == int(o.data.size), (const Dict&)o);
        s << "\nstream\n";
        assert_(o.data.size <= 30 || o.value("Filter","")=="/FlateDecode", o.data.size, deflate(o.data).size, o.value("Filter",""));
        s << o.data;
        s << "\nendstream";
    }
    return s;
}

buffer<byte> toPDF(int2 pageSize, const ref<Graphics> pages, float px) {
    array<unique<Object>> objects;
    auto ref = [&](const Object& object) { return dec(objects.indexOf(&object))+" 0 R"; };

    objects.append(); // Null object

    Object& root = objects.append();
    root.insert("Type", String("/Catalog"));

    Object& pdfPages = objects.append();
    pdfPages.insert("Type", String("/Pages"));
    pdfPages.insert("Kids", array<Variant>());
    pdfPages.insert("Count", 0);

    map<String, String> fonts; // font ID to font ref

    for(const Graphics& graphics: pages) {
        Object& page = objects.append();
        page.insert("Parent", ref(pdfPages));
        page.insert("Type", String("/Page"));
        {array<Variant> mediaBox;
            mediaBox.append( 0 ).append( 0 ).append(pageSize.x*px).append(pageSize.y*px);
            page.insert("MediaBox", move(mediaBox));}
        //page.insert("UserUnit", userUnit); log(userUnit); // Minimum user unit is 1
        {Object& contents = objects.append();
            String content;
            content << "BT\n";
            Dict xFonts;
            String fontID; float fontSize=0;
            vec2 last = 0;
            for(const Glyph& glyph: graphics.glyphs) { // FIXME: Optimize redundant state changes
                const Font& font = glyph.font;
                if(font.name != fontID || font.size != fontSize) {
                    if(!xFonts.contains(font.name)) {
                        if(!fonts.contains(font.name)) {
                            Object& xFont = objects.append();
                            xFont.insert("Type", String("/Font"));
                            xFont.insert("Subtype", String("/Type0"));
                            xFont.insert("BaseFont", String("/Font"));
                            xFont.insert("Encoding", String("/Identity-H"));
                            {array<Variant> descendantFonts;
                                {Dict cidFont;
                                    cidFont.insert("Type", String("/Font"));
                                    cidFont.insert("Subtype", String("/CIDFontType2"));
                                    cidFont.insert("BaseFont", String("/Font"));
                                    {Dict cidSystemInfo;
                                        cidSystemInfo.insert("Registry","(Adobe)");
                                        cidSystemInfo.insert("Ordering","(Identity)");
                                        cidSystemInfo.insert("Supplement", 0);
                                        cidFont.insert("CIDSystemInfo", move(cidSystemInfo));}
                                    {Object& fontDescriptor = objects.append();
                                        fontDescriptor.insert("Type","/FontDescriptor");
                                        fontDescriptor.insert("FontName","/Font");
                                        fontDescriptor.insert("Flags", 1<<3 /*Symbolic*/);
                                        {array<Variant> fontBBox;
                                            fontBBox.append(dec(font.bboxMin .x)).append(dec(font.bboxMin .y))
                                                    .append(dec(font.bboxMax.x)).append(dec(font.bboxMax.y));
                                            fontDescriptor.insert("FontBBox", move(fontBBox));}
                                        fontDescriptor.insert("ItalicAngle", 0);
                                        fontDescriptor.insert("Ascent", dec(font.ascender));
                                        fontDescriptor.insert("Descent", dec(font.descender));
                                        fontDescriptor.insert("StemV", 1);
                                        {Object& fontFile = objects.append();
                                            fontFile.insert("Filter","/FlateDecode");
                                            fontFile = deflate(font.data);
                                            fontDescriptor.insert("FontFile2", ref(fontFile));}
                                        cidFont.insert("FontDescriptor", ref(fontDescriptor));}
                                    cidFont.insert("CIDToGIDMap","/Identity");
                                    descendantFonts << move(cidFont);}
                                xFont.insert("DescendantFonts", move(descendantFonts));}
                            //TODO: ToUnicode
                            fonts.insert(copy(font.name), ref(xFont));
                        }
                        xFonts.insert(font.name, fonts.at(font.name));
                    }
                    content << '/'+font.name+' '+dec(glyph.font.size*px)+" Tf\n"; // Font size in pixels
                }
                uint index = font.index(glyph.code);
                assert_(index < 1<<15);
                vec2 origin = vec2(glyph.origin.x, pageSize.y-glyph.origin.y)*px;
                vec2 relative = origin - last; // Position update of glyph origin in pixels
                last = origin;
                content << str(relative.x,relative.y)+" Td <"+hex(index,4)+"> Tj\n";
            }
            content << "ET\n";

            {Dict resources;
                if(graphics.blits) {
                    map<String, Variant> xObjects;
                    for(int index: range(graphics.blits.size)) {
                        const Blit& blit = graphics.blits[index];
                        const auto& image = blit.image;
                        String id = "Image"+str(pdfPages.at("Count").number)+str(index);
                        {Object& xImage = objects.append();
                            xImage.insert("Subtype", String("/Image"));
                            xImage.insert("Width", dec(image.width));
                            xImage.insert("Height", dec(image.height));
                            xImage.insert("ColorSpace", String("/DeviceRGB"));
                            xImage.insert("BitsPerComponent", String("8"));
                            buffer<byte3> rgb3 (image.height * image.width); static_assert(sizeof(byte3)==3, "");
                            for(uint y: range(image.height)) for(uint x: range(image.width)) rgb3[y*image.width+x] = image.data[y*image.stride+x].rgb();
                            xImage.insert("Filter","/FlateDecode");
                            xImage = deflate(cast<byte>(rgb3));
                            xObjects.insert(copy(id), ref(xImage));
                        }
                        assert_(image.width && image.height, image.size);
                        content << "q "+str(blit.size.x*px,0,0,blit.size.y*px, blit.origin.x*px, (pageSize.y-blit.origin.y-blit.size.y)*px)+" cm /"+id+" Do Q\n";
                    }
                    resources.insert("XObject", move(xObjects));
                }
                resources.insert("Font", move(xFonts));
                page.insert("Resources", move(resources));
            }

            for(auto& line: graphics.lines) content << str(line.a.x*px, (pageSize.y-line.a.y)*px)+" m "+str(line.b.x*px, (pageSize.y-line.b.y)*px)+" l S\n";

            contents.insert("Filter","/FlateDecode");
            contents = deflate(content);
            page.insert("Contents", ref(contents));
        }
        pdfPages.at("Kids").list << ref(page);
        pdfPages.at("Count").number++;
    }
    root.insert("Pages", ref(pdfPages));

    array<byte> file = String("%PDF-1.7\n");
    array<uint> xrefs (objects.size);
    for(uint index: range(1, objects.size)) {
        xrefs << file.size;
        file << dec(index) << " 0 obj\n" << str(objects[index]) << "\nendobj\n";
    }
    int index = file.size;
    file << "xref\n0 " << dec(xrefs.size+1) << "\n0000000000 65535 f\r\n";
    for(uint index: xrefs) {
        String entry (20);
        entry << dec(index, 10, '0') << " 00000 n\r\n";
        assert(entry.size==20);
        file << entry;
    }
    Dict trailer; trailer.insert("Size", int(1+xrefs.size)); assert_(objects); trailer.insert("Root", ref(root));
    file << "trailer " << str(trailer) << "\n";
    file << "startxref\n" << dec(index) << "\r\n%%EOF";
    return move(file);
}
