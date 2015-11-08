#include "MusicXML.h"
#include "sheet.h"
#include "ui/layout.h"
#include "interface.h"
#include "window.h"
#include "pdf.h"

struct MusicPDF {
    // Name
    string name = arguments() ? arguments()[0] : (error("Expected name"), string());
    // MusicXML
    MusicXML xml = readFile(name+".xml"_);
    // Page
    static constexpr float inchMM = 25.4;//, inchPx = 90;
    //const int2 pageSize = int2(210/*mm*/ * (inchPx/inchMM), 297/*mm*/ * (inchPx/inchMM));
    const int2 pageSizeMM = int2(210, 297);
    const int2 pageSize = int2(pageSizeMM.x*1024/pageSizeMM.y, 1024);
    const float inchPx = pageSize.y/pageSizeMM.y*inchMM;
    // Sheet
    Sheet sheet {xml.signs, xml.divisions, 0, 7/*mm*/*(inchPx/inchMM) / 8/*half intervals / staff height*//*, {}, name, true*/};
};

struct MusicPDFPreview : MusicPDF, Application {
    Scroll<HList<GraphicsWidget> > pages {apply(sheet.pages, [](Graphics& o) { return GraphicsWidget(move(o));})};
    unique<Window> window = ::window(&pages, sheet.pageSize ?: int2(sheet.pages[0].bounds.max), mainThread, false, str(name, pages.size));
};
registerApplication(MusicPDFPreview);

struct MusicPDFExport : MusicPDF, Application {
    MusicPDFExport() {
        writeFile(name+".pdf"_, toPDF(vec2(sheet.pageSize) ?: sheet.pages[0].bounds.max, sheet.pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), home(), true);
    }
};
registerApplication(MusicPDFExport, export);
