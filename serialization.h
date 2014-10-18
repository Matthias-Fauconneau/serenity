#pragma once
#include "data.h"
#include "map.h"
#include "file.h"
#include "function.h"

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
