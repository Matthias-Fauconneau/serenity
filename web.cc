/// \file web.cc Generates HTML pages
#include "thread.h"

struct Web : Application {
	String parseLine(const Folder& folder, string line, const map<string,string>& arguments={}) {
		String out;
		TextData s (line);
		while(s) {
			out.append( s.whileNot('$') );
			if(s.match('$')) {
				string parameter = s.identifier();
				String value;
				/**/  if(arguments.contains(parameter)) value = String(arguments.at(parameter));
				else if(parameter=="PWD") value=folder.name();
				else if(parameter=="FOLDER") {
					return join(apply(folder.list(Folders|Sorted),[&](string subfolder){
						auto nextArguments = copy(arguments);
						nextArguments.insert("FOLDER", subfolder);
						return parseLine(folder, line, nextArguments);
					}), "\n");
				} else if(parameter=="FILE") {
					return join( apply( folder.list(Files|Sorted).filter([](string file){return !endsWith(toLower(file),".jpg");}), [&](string file) {
						auto nextArguments = copy(arguments);
						nextArguments.insert("FILE", file);
						return parseLine(folder, line, nextArguments);
					}), "\n");
				} else error(parameter);
				if(s.match('$')) value = String(trim(section(section(value,'/',-2,-1),'.',-2,-1)));
				out.append(value);
			}
		}
		return out;
	}

	String parseFile(const Folder& folder, string file) {
		String out;
		for(TextData s(file); s;) out.append( parseLine(folder, s.line())+"\n" );
		return out;
	}

	Web() {
		Folder root = arguments() ? Folder(arguments()[0]) : Folder(".");
		//writeFile("index.html", parseFile(root, readFile("index.template", root)), root, false);
		for(string name: root.list(Folders|Sorted)) {
			Folder folder(name, root);
			auto instance = parseFile(folder, readFile("+index.template", root));
			writeFile("index.html", instance, folder, true);
		}
	}
} app;
