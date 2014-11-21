#include "thread.h"
#include "MusicXML.h"

struct Score { String name; uint noteCount; };
bool operator ==(const Score& a, string name) { return a.name == name; }
bool operator <(const Score& a, const Score& b) { return a.noteCount < b.noteCount; }
String str(const Score& o) { return str(o.noteCount, 4, ' ')+' '+o.name; }

struct Statistics {
	Statistics() {
		Folder folder("Documents/Scores", home());
		array<Score> scores;
		for(string path: folder.list(Recursive|Files)) {
			if(!endsWith(path, ".xml")) continue;
			string name = section(section(path,'/',-2,-1),'.');
			if(scores.contains(name)) error("Duplicate", name, path);
			MusicXML xml(readFile(path,folder), name);
			if(!xml.signs.size) { log(name, "failed"); continue; }
			uint noteCount = 0;
			for(Sign sign: xml.signs) if(sign.type==Sign::Note && (sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStart))
				noteCount++;
			scores.insertSorted({copyRef(name), noteCount});
		}
		log(str(scores,"\n"_));
	}
} app;
