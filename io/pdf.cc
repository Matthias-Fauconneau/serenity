#include "pdf.h"
#include "variant.h"
#include "data.h"
#include "matrix.h"
#include "deflate.h"
#include "time.h"

generic String str(const map<T,Variant>& dict) {
	array<char> s;
    s.append("<<"); for(auto entry: dict) s.append( '/'+entry.key+' '+str(entry.value)+' ' ); s.append(">>");
	return move(s);
}

typedef map<String,Variant> Dict;

struct Object : Dict {
    buffer<byte> data;

    void operator =(buffer<byte>&& data) {
        this->data = move(data);
		insert("Length"__, this->data.size);
    }
};

inline String str(const Object& o) {
	array<char> s = str((const Dict&)o);
    if(o.data) {
        assert_(o.at("Length").integer() == int(o.data.size), (const Dict&)o);
		//assert_(o.data.size <= 30 || o.value("Filter","")=="/FlateDecode", o.data.size, deflate(o.data).size, o.value("Filter",""));
		s.append("\nstream\n"+o.data+"\nendstream");
    }
	return move(s);
}

buffer<byte> toPDF(int2 pageSize, const ref<Graphics> pages, float px) {
    array<unique<Object>> objects;
	auto ref = [&](const Object& object) { return str(objects.indexOf(&object))+" 0 R"; };

    objects.append(); // Null object

    Object& root = objects.append();
	root.insert("Type"__, "/Catalog");

    Object& pdfPages = objects.append();
	pdfPages.insert("Type"__, "/Pages");
	pdfPages.insert("Kids"__, Variant(array<Variant>()));
	pdfPages.insert("Count"__, 0);

    map<String, String> fonts; // font ID to font ref

    for(const Graphics& graphics: pages) {
		log(pdfPages.at("Count"));
        Object& page = objects.append();
		page.insert("Parent"__, ref(pdfPages));
		page.insert("Type"__, "/Page");
        {array<Variant> mediaBox;
			mediaBox.append( 0 ); mediaBox.append( 0 ); mediaBox.append(pageSize.x*px); mediaBox.append(pageSize.y*px);
			page.insert("MediaBox"__, Variant(move(mediaBox)));}
        //page.insert("UserUnit", userUnit); log(userUnit); // Minimum user unit is 1
        {Object& contents = objects.append();
			array<char> content;
			content.append("BT\n");
            Dict xFonts;
            String fontID; float fontSize=0;
            vec2 last = 0;
            for(const Glyph& glyph: graphics.glyphs) { // FIXME: Optimize redundant state changes
                const Font& font = glyph.font;
                if(font.name != fontID || font.size != fontSize) {
                    if(!xFonts.contains(font.name)) {
                        if(!fonts.contains(font.name)) {
                            Object& xFont = objects.append();
							xFont.insert("Type"__, "/Font");
							xFont.insert("Subtype"__, "/Type0");
							xFont.insert("BaseFont"__, "/Font");
							xFont.insert("Encoding"__, "/Identity-H");
                            {array<Variant> descendantFonts;
                                {Dict cidFont;
									cidFont.insert("Type"__, "/Font");
									cidFont.insert("Subtype"__, "/CIDFontType2");
									cidFont.insert("BaseFont"__, "/Font");
                                    {Dict cidSystemInfo;
										cidSystemInfo.insert("Registry"__, "(Adobe)");
										cidSystemInfo.insert("Ordering"__, "(Identity)");
										cidSystemInfo.insert("Supplement"__, 0);
										cidFont.insert("CIDSystemInfo"__, Variant(move(cidSystemInfo)));}
                                    {Object& fontDescriptor = objects.append();
										fontDescriptor.insert("Type"__, "/FontDescriptor");
										fontDescriptor.insert("FontName"__, "/Font");
										fontDescriptor.insert("Flags"__, 1<<3 /*Symbolic*/);
                                        {array<Variant> fontBBox;
											fontBBox.append(str(int(font.bboxMin .x))); fontBBox.append(str(int(font.bboxMin .y)));
											fontBBox.append(str(int(font.bboxMax.x))); fontBBox.append(str(int(font.bboxMax.y)));
											fontDescriptor.insert("FontBBox"__, Variant(move(fontBBox)));}
										fontDescriptor.insert("ItalicAngle"__, 0);
										fontDescriptor.insert("Ascent"__, int(font.ascender));
										fontDescriptor.insert("Descent"__, int(font.descender));
										fontDescriptor.insert("StemV"__, 1);
                                        {Object& fontFile = objects.append();
											fontFile.insert("Filter"__, "/FlateDecode");
                                            fontFile = deflate(font.data);
											fontDescriptor.insert("FontFile2"__, ref(fontFile));}
										cidFont.insert("FontDescriptor"__, ref(fontDescriptor));}
									cidFont.insert("CIDToGIDMap"__, "/Identity");
									descendantFonts.append(move(cidFont));}
								xFont.insert("DescendantFonts"__, Variant(move(descendantFonts)));}
                            //TODO: ToUnicode
                            fonts.insert(copy(font.name), ref(xFont));
                        }
						xFonts.insert(copy(font.name), copy(fonts.at(font.name)));
                    }
					content.append('/'+font.name+' '+str(int(glyph.font.size*px))+" Tf\n"); // Font size in pixels
                }
                uint index = font.index(glyph.code);
                assert_(index < 1<<15);
                vec2 origin = vec2(glyph.origin.x, pageSize.y-glyph.origin.y)*px;
                vec2 relative = origin - last; // Position update of glyph origin in pixels
                last = origin;
				content.append(str<int>(relative.x,relative.y)+" Td <"+hex(index,4)+"> Tj\n");
            }
			content.append("ET\n");

            {Dict resources;
                if(graphics.blits) {
                    map<String, Variant> xObjects;
                    for(int index: range(graphics.blits.size)) {
                        const Blit& blit = graphics.blits[index];
						const Image& image = blit.image;
                        String id = "Image"+str(pdfPages.at("Count").number)+str(index);
                        {Object& xImage = objects.append();
							xImage.insert("Subtype"__, "/Image");
							xImage.insert("Width"__, image.width);
							xImage.insert("Height"__, image.height);
							xImage.insert("ColorSpace"__, "/DeviceRGB");
							xImage.insert("BitsPerComponent"__, 8);
							typedef vec<rgb,uint8,3> rgb3;
							buffer<rgb3> rgb (image.height * image.width);
							for(uint y: range(image.height)) for(uint x: range(image.width)) rgb[y*image.width+x] = image[y*image.stride+x];
							xImage.insert("Filter"__, "/FlateDecode");
							xImage = deflate(cast<byte>(rgb));
                            xObjects.insert(copy(id), ref(xImage));
                        }
                        assert_(image.width && image.height, image.size);
						content.append("q "+str(blit.size.x*px,0,0,blit.size.y*px, blit.origin.x*px, (pageSize.y-blit.origin.y-blit.size.y)*px)+
									   " cm /"+id+" Do Q\n");
                    }
					resources.insert("XObject"__, move(xObjects));
                }
				resources.insert("Font"__, move(xFonts));
				page.insert("Resources"__, move(resources));
            }

			for(auto& line: graphics.lines)
				content.append(str(line.a.x*px, (pageSize.y-line.a.y)*px)+" m "+str(line.b.x*px, (pageSize.y-line.b.y)*px)+" l S\n");

			contents.insert("Filter"__, "/FlateDecode");
            contents = deflate(content);
			page.insert("Contents"__, ref(contents));
        }
		pdfPages.at("Kids").list.append( ref(page) );
        pdfPages.at("Count").number++;
    }
	root.insert("Pages"__, ref(pdfPages));

	string header = "%PDF-1.7\n";
	size_t fileByteIndex = header.size;
	buffer<buffer<byte>> pdfObjects =
			apply(objects.size-1, [&](size_t index) -> buffer<byte> { return str(1+index)+" 0 obj\n"+str(objects[1+index])+"\nendobj\n"; });
	String xrefHeader = "xref\n0 "+str(1+objects.size)+"\n0000000000 65535 f\r\n";
	String xrefTable ((objects.size-1)*20, 0);
	for(size_t index: range(objects.size-1)) {
		xrefTable.append(str(fileByteIndex, 10)+" 00000 n\r\n");
		fileByteIndex += pdfObjects[index].size;
    }
	size_t contentSize = fileByteIndex;
	size_t xrefTableStart = fileByteIndex;
	Dict trailerDict; trailerDict.insert("Size"__, 1+objects.size); trailerDict.insert("Root"__, ref(root));
	String trailer = ("trailer "+str(trailerDict)+"\n")+("startxref\n"+str(xrefTableStart)+"\r\n%%EOF");
	buffer<byte> file(header.size+contentSize+xrefHeader.size+xrefTable.size+trailer.size, 0);
	file.append(header);
	for(size_t index: range(objects.size-1)) file.append(pdfObjects[index]);
	file.append(xrefHeader);
	file.append(xrefTable);
	file.append(trailer);
	return file;
}
