/// \file web.cc Generates HTML pages
#include "thread.h"
#include "data.h"

struct Web : Application {
	String parseLine(const Folder& root, const Folder& folder, string line, const map<string,string>& arguments={}, size_t index = 0, size_t listSize=0) {
		array<char> out;
		TextData s (line);
		while(s) {
			out.append( s.whileNot('$') );
			if(s.match('$')) {
				string parameter = s.identifier("#+");
				String value;
				/**/  if(arguments.contains(parameter)) value = copyRef(arguments.at(parameter));
				else if(parameter=="PWD") value=folder.name();
				else if(parameter=="FOLDER") {
					auto folders = root.list(Folders|Sorted);
					folders.filter([](string name){return !name.contains('.');});
					return join(apply(folders, [&](string subfolder){
						auto nextArguments = copy(arguments);
						nextArguments.insert("FOLDER", subfolder);
						return parseLine(root, folder, line, nextArguments);
					}), "\n");
				} else if(parameter=="FILE") {
					auto files = folder.list(Files|Sorted);
					files.filter([](string name){return !endsWith(toLower(name),".jpg");});
					return join( apply(files.size, [&](size_t index) {
									 string file = files[index];
									 map<string,string> nextArguments = copy(arguments);
									 nextArguments.insert("FILE", file);
									 return parseLine(root, folder, line, nextArguments, index, files.size);
								 }), "\n");
				} else if(startsWith(parameter, "#"_)) {
					if(parameter=="#") value = str(index);
					else if(parameter=="#+1") { if(index+1<listSize) value = str(index+1); }
					else error(parameter);
				} else {
					auto files = folder.list(Files|Sorted);
					if(files.contains(parameter)) out.append( readFile(parameter, folder) );
					else error(parameter, files);
				}
				if(s.match('?')) { // Conditionnal content
					string content = s.wouldMatch('<') ? s.until('>') : s.until(' ');
					if(value) out.append(parseLine(root, folder, content, arguments, index, listSize));
				} else {
					if(s.match('$')) value = copyRef(trim(section(section(value,'/',-2,-1),'.',-2,-1)));
					out.append(value);
				}
			}
		}
		return move(out);
	}

	String parseFile(const Folder& root, const Folder& folder, string file) {
		array<char> out;
		for(TextData s(file); s;) out.append( parseLine(root, folder, s.line())+"\n" );
		return move(out);
	}

	Web() {
		Folder root = arguments() ? Folder(arguments()[0]) : Folder(".");
		//writeFile("index.html", parseFile(root, readFile("index.template", root)), root, false);
		for(string name: root.list(Folders|Sorted)) {
			if(!name.contains('.')) continue;
			Folder folder(name, root);
			auto instance = parseFile(root, folder, readFile("+index.template", root));
			writeFile("index.html", instance, folder, true);
		}
	}
} app;
