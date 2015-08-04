#include "pdf.h"
#include "variant.h"
#include "data.h"
#include "matrix.h"
#include "deflate.h"

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
  //assert_(o.data.size <= 30 || o.value("Filter","")=="/FlateDecode", o.data.size, deflate(o.data).size, o.value("Filter",""_));
  assert_(!o.contains("Filter") || (string)o.at("Filter")=="/FlateDecode"_, o.at("Filter"));
  s.append("\nstream\n"+o.data+"\nendstream");
 }
 return move(s);
}

buffer<byte> toPDF(vec2 pageSize, const ref<Graphics> pages, float px) {
 array<unique<Object>> objects;
 auto ref = [&](const Object& object) -> String { return str(objects.indexOf(&object))+" 0 R"; };

 objects.append(); // Null object

 Object& root = objects.append();
 root.insert("Type"__, "/Catalog"_);

 Object& pdfPages = objects.append();
 pdfPages.insert("Type"__, "/Pages"_);
 pdfPages.insert("Kids"__, Variant(array<Variant>()));
 pdfPages.insert("Count"__, 0);

 map<String, String> fonts; // font ID to font ref
 map<String, String> graphicStates; // graphic state ID to graphic state ref

 assert_(pages);
 for(const Graphics& graphics: pages) {
  if(graphics.fills) log("WARNING: unsupported fills, skipping", graphics.fills.size, "elements, use parallelograms");
  Object& page = objects.append();
  page.insert("Parent"__, ref(pdfPages));
  page.insert("Type"__, "/Page"_);
  {array<Variant> mediaBox;
   mediaBox.append( 0 ); mediaBox.append( 0 ); mediaBox.append(pageSize.x*px); mediaBox.append(pageSize.y*px);
   page.insert("MediaBox"__, Variant(move(mediaBox)));}
  {Object& contents = objects.append();
   array<char> content;

   Dict pageGraphicStateReferences;
   float currentOpacity=1;
   auto setOpacity = [&](float opacity) {
    if(opacity != currentOpacity) {
     if(!pageGraphicStateReferences.contains(str(opacity))) {
      if(!graphicStates.contains(str(opacity))) {
       Object& graphicState = objects.append();
       graphicState.insert("Type"__, "/ExtGState"_);
       graphicState.insert("CA"__, opacity);
       graphicState.insert("ca"__, opacity);
       graphicStates.insert(str(opacity), ref(graphicState));
      }
      pageGraphicStateReferences.insert(str(opacity), copy(graphicStates.at(str(opacity))));
     }
     content.append("/"_+str(opacity)+" gs\n");
     currentOpacity = opacity;
    }
   };

   content.append("BT\n");
   Dict pageFontReferences;
   String fontID; float fontSize=0;
   vec2 last = 0;
   for(const Glyph& glyph: graphics.glyphs) {
    const FontData& font = glyph.font;
    if(font.name != fontID || glyph.fontSize != fontSize) {
     if(!pageFontReferences.contains(font.name)) {
      if(!fonts.contains(font.name)) {
       Object& xFont = objects.append();
       xFont.insert("Type"__, "/Font"_);
       xFont.insert("Subtype"__, "/Type0"_);
       xFont.insert("BaseFont"__, "/Font"_);
       xFont.insert("Encoding"__, "/Identity-H"_);
       {array<Variant> descendantFonts;
        {Dict cidFont;
         cidFont.insert("Type"__, "/Font"_);
         cidFont.insert("Subtype"__, "/CIDFontType2"_);
         cidFont.insert("BaseFont"__, "/Font"_);
         {Dict cidSystemInfo;
          cidSystemInfo.insert("Registry"__, "(Adobe)"_);
          cidSystemInfo.insert("Ordering"__, "(Identity)"_);
          cidSystemInfo.insert("Supplement"__, 0);
          cidFont.insert("CIDSystemInfo"__, Variant(move(cidSystemInfo)));}
         {Object& fontDescriptor = objects.append();
          fontDescriptor.insert("Type"__, "/FontDescriptor"_);
          fontDescriptor.insert("FontName"__, "/Font"_);
          fontDescriptor.insert("Flags"__, 1<<3 /*Symbolic*/);
          /*{array<Variant> fontBBox;
           fontBBox.append(str(int(font.bboxMin .x))); fontBBox.append(str(int(font.bboxMin .y)));
           fontBBox.append(str(int(font.bboxMax.x))); fontBBox.append(str(int(font.bboxMax.y)));
           fontDescriptor.insert("FontBBox"__, Variant(move(fontBBox)));}*/
          fontDescriptor.insert("ItalicAngle"__, 0);
          //fontDescriptor.insert("Ascent"__, int(font.ascender));
          //fontDescriptor.insert("Descent"__, int(font.descender));
          fontDescriptor.insert("StemV"__, 1);
          {Object& fontFile = objects.append();
           fontFile.insert("Filter"__, "/FlateDecode"_);
           fontFile = deflate(font.data);
           fontDescriptor.insert("FontFile2"__, ref(fontFile));}
          cidFont.insert("FontDescriptor"__, ref(fontDescriptor));}
         cidFont.insert("CIDToGIDMap"__, "/Identity"_);
         descendantFonts.append(move(cidFont));}
        xFont.insert("DescendantFonts"__, Variant(move(descendantFonts)));}
       //TODO: ToUnicode
       fonts.insert(copy(font.name), ref(xFont));
      }
      pageFontReferences.insert(copy(font.name), copy(fonts.at(font.name)));
     }
     content.append('/'+font.name+' '+str(int(glyph.fontSize*px))+" Tf\n"); // Font size in pixels
    }

    uint index = glyph.index; //font.index(glyph.code);
    assert_(index < 1<<15);
    vec2 origin = vec2(glyph.origin.x, pageSize.y-glyph.origin.y)*px;
    vec2 relative = origin - last; // Position update of glyph origin in pixels
    last = origin;
    setOpacity(glyph.opacity);
    content.append(str(relative.x,relative.y)+" Td <"+hex(index,4)+"> Tj\n");
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
       xImage.insert("Subtype"__, "/Image"_);
       xImage.insert("Width"__, image.width);
       xImage.insert("Height"__, image.height);
       xImage.insert("ColorSpace"__, "/DeviceRGB"_);
       xImage.insert("BitsPerComponent"__, 8);
       typedef vec<rgb,uint8,3> rgb3;
       typedef vec<rgb,int,3> rgb3i;
       buffer<rgb3> rgb (image.height * image.width);
       for(uint y: range(image.height)) for(uint x: range(image.width)) {
        auto source = image[y*image.stride+x];
        rgb[y*image.width+x] = rgb3(rgb3i(source.r, source.g, source.b)*int(source.a)/0xFF) + rgb3(0xFF-source.a);
       }
       xImage.insert("Filter"__, "/FlateDecode"_);
       xImage = deflate(cast<byte>(rgb));
       xObjects.insert(copy(id), ref(xImage));
      }
      assert_(image.width && image.height, image.size);
      setOpacity(blit.opacity);
      content.append("q "+str(blit.size.x*px,0,0,blit.size.y*px, blit.origin.x*px, (pageSize.y-blit.origin.y-blit.size.y)*px)+
                     " cm /"+id+" Do Q\n");
     }
     resources.insert("XObject"__, move(xObjects));
    }
    resources.insert("Font"__, move(pageFontReferences));
    resources.insert("ExtGState"__, move(pageGraphicStateReferences));
    page.insert("Resources"__, move(resources));
   }

   auto P = [&](vec2 p) { return str(p.x*px, (pageSize.y-p.y)*px); };
   content.append("0 w\n");
   setOpacity(1); // Always reset opacity to 1 for proper print of "cosmetic" (single dot) lines.
   for(auto& line: graphics.lines) {
    content.append(P(line.a)+" m "+P(line.b)+" l S\n");
   }

   for(auto& p: graphics.trapezoids) {
    setOpacity(p.opacity);
    content.append(
       P(vec2(p.span[0].x, p.span[0].min))+" m"
      " "+P(vec2(p.span[0].x, p.span[0].max))+" l"
      " "+P(vec2(p.span[1].x, p.span[0].max))+" l"
      " "+P(vec2(p.span[1].x, p.span[0].min))+" l"
      "f\n");
   }

   for(auto& c: graphics.cubics) {
    setOpacity(c.opacity);
    content.append(P(c.points[0])+" m ");
    assert_(c.points.size%3==0);
    for(size_t index=1; index<c.points.size; index+=3)
     content.append(P(c.points[index])+" "+P(c.points[index+1])+" "+P(c.points[(index+2)%c.points.size])+" c ");
    content.append("f\n");
   }

   contents.insert("Filter"__, "/FlateDecode"_);
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
   apply(objects.size-1, [&](size_t index) -> buffer<byte> { return str(1+index)+" 0 obj\n"_+str(objects[1+index])+"\nendobj\n"_; });
String xrefHeader = "xref\n0 "+str(objects.size)+"\n0000000000 65535 f\r\n";
String xrefTable ((objects.size-1)*20, 0);
for(::ref<byte> o: pdfObjects) { xrefTable.append(str(fileByteIndex, 10u)+" 00000 n\r\n"); fileByteIndex += o.size; }
size_t contentSize = fileByteIndex;
size_t xrefTableStart = fileByteIndex;
Dict trailerDict; trailerDict.insert("Size"__, objects.size); trailerDict.insert("Root"__, ref(root));
String trailer = ("trailer "+str(trailerDict)+"\n")+("startxref\n"+str(xrefTableStart)+"\r\n%%EOF");
buffer<byte> file(contentSize+xrefHeader.size+xrefTable.size+trailer.size, 0);
file.append(header);
for(::ref<byte> o: pdfObjects) file.append(o);
file.append(xrefHeader);
file.append(xrefTable);
file.append(trailer);
assert_(file.size == file.capacity);
log(file, file.size);
return file;
}
