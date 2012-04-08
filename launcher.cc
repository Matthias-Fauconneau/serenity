#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"

#include "array.cc"
template struct array<Command>;

const string iconPaths[4] = {
    "/usr/share/pixmaps/"_,
    "/usr/share/icons/oxygen/$size/apps/"_,
    "/usr/share/icons/hicolor/$size/apps/"_,
    "/usr/local/share/icons/hicolor/$size/apps/"_
};

bool Search::keyPress(Key key) {
    if(key == Return) {
        execute("/usr/bin/chromium-browser"_,{"google.com/search?q="_+text});
        text.clear(); update(); triggered.emit(); return true;
    }
    else return TextInput::keyPress(key);
}

bool Command::mouseEvent(int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(path,args); triggered.emit(); return true; }
    return false;
}

map<string,string> readSettings(const string& path) {
    map<string,string> entries;
    if(!exists(path)) { warn("Missing settings file",path); return entries; }
    for(TextBuffer s(readFile(path));s;) {
        if(s.match("["_)) s.until("\n"_);
        else {
            string key = s.until("="_), value=s.until("\n"_);
            entries.insert(move(key),move(value));
        }
        s.whileAny("\n"_);
    }
    return entries;
}

List<Command> readShortcuts() {
    List<Command> shortcuts;
    if(!exists(".config/launcher"_,home())) { warn("No launcher settings [.config/launcher]"); return shortcuts; }
    for(const string& desktop: split(readFile(".config/launcher"_,home()),'\n')) {
        if(!exists(desktop)) continue;
        auto entries = readSettings(desktop);
        Image icon;
        for(const string& folder: iconPaths) {
            string path = replace(folder,"$size"_,"32x32"_)+entries["Icon"_]+".png"_;
            if(exists(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
        }
        auto execPaths = {""_, "/usr/bin/"_,"/usr/local/bin/"_};
        string path; array<string> arguments;
        for(const string& folder: execPaths) {
            string p = folder+section(entries["Exec"_],' ');
            if(exists(p)) { path=move(p); arguments=slice(split(entries["Exec"_],' '),1); break; }
        }
        if(!path) { warn("Executable not found for",section(entries["Exec"_],' ')); continue; }
        for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries["Name"_]);
        for(uint i=0;i<arguments.size();) if(contains(arguments[i],'%')) arguments.removeAt(i); else i++;
        shortcuts << Command(move(icon),move(entries["Name"_]),move(path),move(arguments));
    }
    return shortcuts;
}

Launcher::Launcher() : shortcuts(readShortcuts()), menu(i({ &search, &shortcuts })), window(&menu,""_,Image(),int2(-3,-3)) {
    window.setType(Atom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"));
    window.setOverrideRedirect(true);
    menu.close.connect(&window,&Window::hide);
    search.triggered.connect(&window,&Window::hide);
    for(auto& shortcut: shortcuts) shortcut.triggered.connect(&window,&Window::hide);
}

void Launcher::show() { window.show(); window.setFocus(&search); }
void Launcher::keyPress(Key key) { if(key==Escape) window.hide(); }
