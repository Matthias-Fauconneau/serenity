/// \file dust.cc Automatic dust removal
#include "processed-source.h"
#include "inverse-attenuation.h"
#include "image-source-view.h"
#include "png.h"

struct CalibrationView : Application {
    ImageFolder folder1 {Folder("Pictures", home())};
    ImageFolder folder2 {Folder("Paper", folder1.folder)};
    Calibration calibration1 {folder1};
    Calibration calibration2 {folder2};
    ImageView views[2]  = {sRGB(calibration1.sum(folder1.maximumSize()/4)),sRGB(calibration2.sum(folder2.maximumSize()/4))};
    WidgetToggle toggleView {&views[0], &views[1]};
    Window window {&toggleView};
    CalibrationView() { log(withName(folder1.count(), folder2.count())); }
};
registerApplication(CalibrationView, calibration);

struct DustRemoval {
    Folder folder {"Pictures", home()};
    ImageFolder calibration {Folder("Paper", folder)};
    InverseAttenuation correction { calibration };
    ImageFolder source { folder,
                [](const String&, const map<String, String>& properties){ return fromDecimal(properties.at("Aperture"_)) <= 5; } };
    ProcessedSource corrected {source, correction};
};

struct DustRemovalTest : DustRemoval, Application {
    DustRemovalTest() { corrected.image(0, source.size(0), true); } // Calibration: 5s, Linear: 0.8s
};
registerApplication(DustRemovalTest, test);

generic T parse(string source);
template<> String parse<String>(string source) { return String(source); }
template<> map<String, String> parse<map<String, String>>(string source) {
	TextData s(source);
	map<String, String> target;
	s.match('{');
	if(s && !s.match('}')) for(;;) {
		s.whileAny(" \t");
		string key = s.identifier();
		s.match(':'); s.whileAny(" \t");
		string value = s.whileNo("},\t\n");
		target.insert(key, trim(value));
		if(!s || s.match('}')) break;
		if(!s.match(',')) s.skip('\n');
	}
	assert_(!s);
	return target;
}

generic struct PersistentValue : T {
	File file;
	function<T()> evaluate;

	PersistentValue(File&& file, function<T()> evaluate={}) : T(parse<T>(string(file.read(file.size())))), file(move(file)), evaluate(evaluate) {}
	PersistentValue(const Folder& folder, string name, function<T()> evaluate={})
		: PersistentValue(File(name, folder, Flags(ReadWrite|Create)), evaluate) {}
	~PersistentValue() { file.seek(0); file.resize(file.write(evaluate ? str(evaluate()) : str((const T&)*this))); }
};
generic String str(const PersistentValue<T>& o) { return str((const T&)o); }

struct DustRemovalPreview : DustRemoval, Application {
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};

	PersistentValue<String> lastName {folder, ".last", [this]{ return source.name(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

    ImageSourceView sourceView {source, &index, window};
    ImageSourceView correctedView {corrected, &index, window};
    WidgetToggle toggleView {&sourceView, &correctedView};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(corrected.name(index)); } };
	DustRemovalPreview() {
		map<string, int> counts;
		for(string attribute: imagesAttributes.values) counts[attribute]++;
		log(counts);

		window.actions[Key('1')] = [this]{ setCurrentImageAttributes("bad"); };
		window.actions[Key('2')] = [this]{ setCurrentImageAttributes("worse"); };
		window.actions[Key('3')] = [this]{ setCurrentImageAttributes("same"); };
		window.actions[Key('4')] = [this]{ setCurrentImageAttributes("better"); };
		window.actions[Key('5')] = [this]{ setCurrentImageAttributes("good"); };
	}

	void setCurrentImageAttributes(string currentImageAttributes) { imagesAttributes[corrected.name(index)] = String(currentImageAttributes); }
};
registerApplication(DustRemovalPreview);

struct DustRemovalExport : DustRemoval, Application {
    DustRemovalExport() {
        Folder output ("Output", folder, true);
        uint64 total = sum(apply(corrected.count(), [this](size_t index) { return product(corrected.size(index)); }));
        log(binaryPrefix(total,"pixels","Pixels"));
        uint64 exported = 0; uint64 compressed = 0;
        for(size_t index: range(corrected.count())) {
            uint size = product(corrected.size(index));
            String name = corrected.name(index);
			Time correctionTime;
			SourceImageRGB image = corrected.image(index, 0, true);
			correctionTime.stop();
			Time compressionTime;
			compressed += writeFile(name, encodePNG(image), output, true);
			compressionTime.stop();
            exported += size;
			log(str(100*(index+1)/corrected.count())+'%',
				'\t',index+1,'/',corrected.count(),
				'\t',exported/1024/1024,'/',total/1024/1024,"MP",
				'\t',corrected.name(index),str(size/1024/1024, 2),"MP", strx(corrected.size(index)),
				'\t',correctionTime, compressionTime);
        }
    }
};
registerApplication(DustRemovalExport, export);
