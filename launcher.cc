#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"

bool Search::keyPress(Key key) {
    if(key == Return) {
        //execute("/usr/bin/chromium-browser"_,{"google.com/search?q="_+text}); TODO: serenity browser
        text.clear(); update(); triggered(); return true;
    }
    else return TextInput::keyPress(key);
}

bool Command::mouseEvent(int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(path,args); triggered(); return true; }
    return false;
}

map<string,string> readSettings(const ref<byte>& path) {
    map<string,string> entries;
    if(!exists(path)) { warn("Missing settings","'"_+path+"'"_); return entries; }
    for(TextStream s(readFile(path));s;) {
        if(s.matchAny("[#"_)) s.until('\n');
        else {
            ref<byte> key = s.until('='), value=s.until('\n');
            entries.insertMulti(string(key),string(value));
        }
        s.whileAny("\n"_);
    }
    return entries;
}

List<Command> readShortcuts() {
    List<Command> shortcuts;
    static int config = openFolder("config"_);
    if(!exists("launcher"_,config)) { warn("No launcher settings [config/launcher]"); return shortcuts; }
    for(const ref<byte>& desktop: split(readFile("launcher"_,config),'\n')) {
        map<string,string> entries = readSettings(desktop);
        Image<byte4> icon;
        for(const ref<byte>& folder: iconPaths) {
            string path = replace(folder,"$size"_,"32x32"_)+entries[string("Icon"_)]+".png"_;
            if(exists(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
        }
        string path = string(section(entries[string("Exec"_)],' '));
        if(!exists(path)) path="/usr/bin/"_+path;
        if(!exists(path)) { warn("Executable not found",path); continue; }
        array<string> arguments;  arguments<<string(section(entries[string("Exec"_)],' ',1,-1));
        for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries[string("Name"_)]);
        for(uint i=0;i<arguments.size();) if(arguments[i].contains('%')) arguments.removeAt(i); else i++;
        shortcuts << Command(move(icon),move(entries[string("Name"_)]),move(path),move(arguments));
    }
    return shortcuts;
}

Launcher::Launcher() {
    window.hideOnLeave = true;
    window.localShortcut(Escape).connect(&window,&Window::hide);
    search.triggered.connect(&window,&Window::hide);
    for(Command& shortcut: shortcuts) shortcut.triggered.connect(&window,&Window::hide);
}
