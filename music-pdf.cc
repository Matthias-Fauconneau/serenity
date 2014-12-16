#include "MusicXML.h"
#include "sheet.h"
#include "window.h"
#include "interface.h"

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
	// Preview
	Window window {&sheet, pageSize, [](){return "MusicPDF"__;}};
	MusicPDF() { window.background = Window::White; window.show(); }
} app;
