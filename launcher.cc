#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"

#include "array.cc"
template struct array<Command>;
template struct Array<Command>;
template struct ListSelection<Command>;
template struct List<Command>;
template struct Popup<Command>;

const string iconPaths[4] = {
    "/usr/share/pixmaps/"_,
    "/usr/share/icons/oxygen/$size/apps/"_,
    "/usr/share/icons/hicolor/$size/apps/"_,
    "/usr/local/share/icons/hicolor/$size/apps/"_
};

bool Search::keyPress(Key key) {
    if(key == Key::Enter) {
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
    if(!exists(path)) { warn("Missing settings","'"_+path+"'"_); return entries; }
    for(TextStream s(readFile(path));s;) {
        if(s.matchAny("[#"_)) s.until('\n');
        else {
            string key = s.until('='), value=s.until('\n');
            entries.insertMulti(move(key),move(value));
        }
        s.whileAny("\n"_);
    }
    return entries;
}

List<Command> readShortcuts() {
    List<Command> shortcuts;
    static int config = openFolder("config"_);
    if(!exists("launcher"_,config)) { warn("No launcher settings [config/launcher]"); return shortcuts; }
    for(const string& desktop: split(readFile("launcher"_,config),'\n')) {
        map<string,string> entries = readSettings(desktop);
        Image<byte4> icon;
        for(const string& folder: iconPaths) {
            string path = replace(folder,"$size"_,"32x32"_)+entries["Icon"_]+".png"_;
            if(exists(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
        }
        string path = section(entries["Exec"_],' ');
        if(!exists(path)) path="/usr/bin/"_+path;
        if(!exists(path)) { warn("Executable not found",path); continue; }
        array<string> arguments = slice(split(entries["Exec"_],' '),1);
        for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries["Name"_]);
        for(uint i=0;i<arguments.size();) if(contains(arguments[i],'%')) arguments.removeAt(i); else i++;
        shortcuts << Command(move(icon),move(entries["Name"_]),move(path),move(arguments));
    }
    return shortcuts;
}

Launcher::Launcher() : shortcuts(readShortcuts()), menu(i({&search, &shortcuts})), window(&menu,""_,Image<byte4>(),int2(-3,-3)) {
    //window.setType(Atom(NET_WM_WINDOW_TYPE_DROPDOWN_MENU));
    //window.setOverrideRedirect(true);
    //window.localShortcut("Leave"_).connect(&window,&Window::hide);
    window.localShortcut(Key::Escape).connect(&window,&Window::hide);
    search.triggered.connect(&window,&Window::hide);
    for(Command& shortcut: shortcuts) shortcut.triggered.connect(&window,&Window::hide);
}

void Launcher::show() { window.show(); window.setFocus(&search); }
