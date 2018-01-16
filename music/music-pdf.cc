#include "MusicXML.h"
#include "sheet.h"
#include "ui/layout.h"
#include "interface.h"
#include "window.h"
#if PDFEXPORT
#include "pdf.h"
#else
#include "image-render.h"
#endif

struct MusicPDF {
    // Name
    string name = arguments() ? arguments()[0] : (error("Expected name"), string());
    // MusicXML
    MusicXML xml = readFile(name+".xml"_);
    // Page
    static constexpr float inchMM = 25.4;
#if 1
    static constexpr float inchPx = 276;
    //const int2 pageSize = int2(210/*mm*/ * (inchPx/inchMM), 297/*mm*/ * (inchPx/inchMM));
#else
    const int2 pageSizeMM = int2(210, 297);
    const int2 pageSize = int2(pageSizeMM.x*1024/pageSizeMM.y, 1024);
    const float inchPx = pageSize.y/pageSizeMM.y*inchMM;
#endif
    // Sheet
    Sheet sheet {xml.signs, xml.divisions, 0, 7/*mm*/*(inchPx/inchMM) / 8/*half intervals / staff height*/, {}, "", false, true};
};

Image render(const Graphics& sheet) {
    const uint N = 4;
    const uint W = 3840, H = sheet.bounds.size().y;
    ImageRenderTarget target(Image(W*N, H));
    target.clear(0xFF);
    ::render(sheet, target);
    Image composite (W, N*H);
    for(uint i: range(N)) {
        copy(cropShare(composite,int2(0,i*H),uint2(W,H)), cropShare(target,int2(i*W,0),uint2(W,H)));
    }
    return ::move(composite);
}

struct MusicPDFPreview : MusicPDF, Application {
    //Scroll<HList<GraphicsWidget> > pages {apply(sheet.pages, [](Graphics& o) { return GraphicsWidget(::move(o));})};
    ImageView pages{render(sheet.pages[0])};
    unique<Window> window = ::window(&pages, /*sheet.pageSize ?: int2(sheet.pages[0].bounds.max)*/-1, mainThread, false, str(name/*, pages.size*/));
};
registerApplication(MusicPDFPreview);

#if PDFEXPORT
struct MusicPDFExport : MusicPDF, Application {
    MusicPDFExport() {
        writeFile(name+"_.pdf"_, toPDF(vec2(sheet.pageSize) ?: sheet.pages[0].bounds.max, sheet.pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), home(), true);
    }
};
registerApplication(MusicPDFExport, export);
#endif
