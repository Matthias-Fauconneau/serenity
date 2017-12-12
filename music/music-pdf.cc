#include "MusicXML.h"
#include "sheet.h"
#include "ui/layout.h"
#include "interface.h"
#include "window.h"
#if PDFEXPORT
#include "pdf.h"
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

struct MusicPDFPreview : MusicPDF, Application {
    Scroll<HList<GraphicsWidget> > pages {apply(sheet.pages, [](Graphics& o) { return GraphicsWidget(::move(o));})};
    unique<Window> window = ::window(&pages, sheet.pageSize ?: int2(sheet.pages[0].bounds.max), mainThread, false, str(name, pages.size));
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
