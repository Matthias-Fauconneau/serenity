#include "layout.h"
#include "function.h"
#include "serialization.h"

/// Parses a layout from files
struct LayoutParse : Layout {
	function<void(string)> logChanged;
	array<char> logText;
	array<char> errors;

	LayoutParse(const struct Folder& folder, struct TextData&& s, function<void(string)> logChanged, struct FileWatcher* watcher = 0);

	// Redirects log, and return on error (instead of aborting)
	template<Type... Args> void log(const Args&... args) {
		auto message = str(args...); this->logText.append(message+"\n"); ::log(message); if(logChanged) logChanged(this->logText);
	}
	template<Type... Args> void error(const Args&... args) {
		auto message = str(args...);
		if(!logChanged) ::error(message);
		errors.append(message+"\n"); this->logText.append(message+"\n"); ::log(message);logChanged(this->logText);
	}

	generic T argument(string name) { assert_(arguments.contains(name)); return parse<T>(arguments.at(name)); };
	generic T value(string name, T defaultValue=T()) { return arguments.contains(name) ? parse<T>(arguments.at(name)) : defaultValue; };
};
