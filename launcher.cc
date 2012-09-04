#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"
#include "png.h"

bool Search::keyPress(Key key) {
    if(key == Return) {
        array<string> args; args<<string("google.com/search?q="_+text); execute("/usr/bin/chromium-browser"_,args,false);
        setText(string()); triggered(); return true;
    }
    else return TextInput::keyPress(key);
}

bool Command::mouseEvent(int2, int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(path,args,false); triggered(); return true; }
    return false;
}

map<string,string> readSettings(const ref<byte>& path) {
    map<string,string> entries;
    if(!existsFile(path)) { warn("Missing settings","'"_+path+"'"_); return entries; }
    for(TextStream s=readFile(path);s;) {
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
    static int config = openFolder(string(getenv("HOME"_)+"/.config"_),root(),true);
    if(!existsFile("launcher"_,config)) { warn("No launcher settings [config/launcher]"); return shortcuts; }
    auto apps = readFile("launcher"_,config);
    for(const ref<byte>& desktop: split(apps,'\n')) {
        if(startsWith(desktop,"#"_)) continue;
        map<string,string> entries = readSettings(desktop);
        Image icon;
        for(const ref<byte>& folder: iconPaths) {
            string path = replace(folder,"$size"_,"32x32"_)+entries[string("Icon"_)]+".png"_;
            if(existsFile(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
        }
        string path = string(section(entries[string("Exec"_)],' '));
        if(!existsFile(path)) path="/usr/bin/"_+path;
        if(!existsFile(path)) { warn("Executable not found",path); continue; }
        array<string> arguments;  arguments<<string(section(entries[string("Exec"_)],' ',1,-1));
        for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries[string("Name"_)]);
        for(uint i=0;i<arguments.size();) if(arguments[i].contains('%')) arguments.removeAt(i); else i++;
        shortcuts << Command(move(icon),move(entries[string("Name"_)]),move(path),move(arguments));
    }
    return shortcuts;
}

Launcher::Launcher() {
    menu<<&search<<&shortcuts;
    window.hideOnLeave = true;
    window.anchor = TopLeft;
    window.localShortcut(Escape).connect(&window,&Window::hide);
    search.triggered.connect(&window,&Window::hide);
    for(Command& shortcut: shortcuts) shortcut.triggered.connect(&window,&Window::hide);
}
