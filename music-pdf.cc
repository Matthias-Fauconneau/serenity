#include "MusicXML.h"
#include "sheet.h"
#include "window.h"
#include "interface.h"
#include "pdf.h"

struct MusicPDF {
	// Name
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
	// MusicXML
	MusicXML xml = readFile(name+".xml"_);
	// Page
	static constexpr float inchMM = 25.4, inchPx = 90;
	const int2 pageSize = int2(210/*mm*/ * (inchPx/inchMM), 297/*mm*/ * (inchPx/inchMM));
	// Sheet
	Sheet sheet {xml.signs, xml.divisions, {}, pageSize, name};
};

struct MusicPDFPreview : MusicPDF, Application {
	Window window {&sheet, pageSize, [this](){return unsafeRef(name);}};
};
registerApplication(MusicPDFPreview);

struct MusicPDFExport : MusicPDF, Application {
	MusicPDFExport() {
		writeFile(name+".pdf"_, toPDF(sheet.pageSize, sheet.pages, inchPx/72/*PostScript point per inch*/), home(), true);
	}
};
registerApplication(MusicPDFExport, export);
