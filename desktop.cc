/// \file desktop.cc Background shell application
#include "process.h"
#include "window.h"
#include "calendar.h"
#include "feeds.h"

typedef double real;
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
const real PI = 3.14159265358979323846;
inline real cos(real t) { return __builtin_cos(t); }
inline real sin(real t) { return __builtin_sin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real acos(real t) { return __builtin_acos(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }
inline real rad(real t) { return t/180*PI; }
struct Sun {
    Date rise, noon, set;
    Sun(float longitude, float latitude) {
        // Orbital parameters
        real e = 0.01671123; // Earth eccentricity
        real Omega = rad(348.74); // Longitude of ascending node
        real omega = rad(114.21); // Argument of perihelion
        real epsilon = rad(23.439); //Obliquity of the ecliptic
        real year = 365.2145; //Sidereal year [solar day]
        real n = 2*PI/(year*24*60*60); //Mean motion [rad/s]

        // Time
        real U = currentTime(); //Unix time
        real T = U - 10957.5*24*60*60; // J2000 time [s]
        real M0 = rad(357.51716); //Mean anomaly at J2000
        real GMST0 = 18.697; //J2000 hour angle of the vernal equinox [h]
        real ratio = 1+1/year; //Sidereal day per solar day
        real GMST = GMST0 + ratio*T/60/60; // Greenwich mean sidereal time [h]

        // Positions
        real M = M0 + n*T; //Mean anomaly
        real vu = M + 2*e*sin(M) + 5/4*e*e*sin(2*M); //True anomaly
        real l = vu + Omega + omega; //Ecliptic longitude (Omega + omega = Longitude of the periapsis)
        real lambda = PI + l; //Ecliptic longitude (geocentric)
        real alpha = atan(cos(epsilon)*sin(lambda), cos(epsilon)*cos(lambda)); //Right ascension
        real delta = asin(sin(epsilon)*sin(lambda)); //Declination

        // Hours
        real h = GMST - (longitude + alpha)/PI*12; // Solar hour angle [h]
        real w0 = acos(-tan(latitude)*tan(delta))/PI*12*60*60; // Half day length [s]
        real noon = U-mod(h,24)*60*60; // Solar noon in unix time [s]
        rise=noon-w0, noon=noon, set=noon+w0;
    }
} sun(-2.1875*PI/180, 48.6993*PI/180); //Orsay, France

struct Runner : TextInput {
    signal<> triggered;
    bool keyPress(Key key, Modifiers modifiers) override;
};

bool Runner::keyPress(Key key, Modifiers modifiers) {
    if(key == Return) {
        array<string> args; args<<string("-ow=http://google.com/search?q="_+toUTF8(text)); execute("/usr/bin/qupzilla"_,args,false);
        setText(string()); triggered(); return true;
    }
    else return TextInput::keyPress(key, modifiers);
}

/// Executes \a path with \a args when pressed
struct Command : Item {
    string path; array<string> args;
    Command(Image&& icon, string&& text, string&& path, array<string>&& args) :
        Linear(Left),Item(move(icon),move(text)),path(move(path)),args(move(args)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button) override;
};

bool Command::mouseEvent(int2, int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(path,args,false); }
    return false;
}

map<ref<byte>,ref<byte>> readSettings(const ref<byte>& file) {
    map<ref<byte>,ref<byte>> entries;
    for(TextData s(file);s;) {
        if(s.matchAny("[#"_)) s.line();
        else {
            ref<byte> key = s.until('='), value=s.line();
            entries.insertMulti(key,value);
        }
        s.whileAny("\n"_);
    }
    return entries;
}

struct Weather : ImageView {
    signal<> contentChanged;
    Weather(){get();}
    void get() { getURL("http://www.yr.no/place/France/Île-de-France/Orsay/meteogram.png"_, {this, &Weather::load}, 60); }
    void load(const URL&, Map&& file) { image = crop(decodeImage(file), int2(6, 24), int2(816, 242)); contentChanged(); }
    bool mouseEvent(int2, int2, Event event, Button) { if(event==Press) get(); return false; }
} test;

/// Displays a feed reader, an event calendar and an application launcher (activated by taskbar home button)
struct Desktop {
    Feeds feeds;
    Scroll<HTML> page;

    Runner runner;
       Text rise{str(sun.rise,"hh:mm"_),12};
       Clock clock{ 64 };
       Text set{str(sun.set,"hh:mm"_),12};
      HBox hourBox{HBox::Spread,VBox::AlignBottom};//{&rise,&clock,&set}
      Events calendar;
     VBox timeBox;//{&clock, &calendar};
     List<Command> shortcuts;
    HBox hbox;//{&feeds, &timeBox, &shortcuts};
    Weather weather;
    VBox vbox {VBox::Share, HBox::Expand};//{&runner, &hbox, &weather};
    Window window{&vbox,0,"Desktop"_,Image(),"_NET_WM_WINDOW_TYPE_DESKTOP"_};
    Window browser{0,0,"Browser"_};
    Desktop() {
        if(!existsFile("launcher"_,config())) log("No launcher settings [.config/launcher]");
        else {
            auto apps = readFile("launcher"_,config());
            for(const ref<byte>& line: split(apps,'\n')) {
                const ref<byte> desktop=trim(line);
                if(!desktop || startsWith(desktop,"#"_)) continue;
                if(!existsFile(desktop)) { warn("Missing settings","'"_+desktop+"'"_); continue; }
                string file = readFile(desktop);
                map<ref<byte>,ref<byte>> entries = readSettings(file);

                static constexpr ref<byte> iconPaths[] = {
                    "/usr/share/pixmaps/"_,
                    "/usr/share/icons/oxygen/32x32/apps/"_,
                    "/usr/share/icons/hicolor/32x32/apps/"_,
                    "/usr/share/icons/oxygen/32x32/actions/"_,
                };
                Image icon;
                for(const ref<byte>& folder: iconPaths) {
                    string path = folder+entries["Icon"_]+".png"_;
                    if(existsFile(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
                }
                assert(icon,entries["Icon"_]);
                string exec = string(section(entries["Exec"_],' '));
                string path = copy(exec);
                if(!existsFile(path)) path="/usr/bin/"_+exec;
                if(!existsFile(path)) path="/usr/local/bin/"_+exec;
                if(!existsFile(path)) path="/bin/"_+exec;
                if(!existsFile(path)) path="/sbin/"_+exec;
                if(!existsFile(path)) { warn("Executable not found",exec); continue; }
                array<string> arguments;  arguments<<string(section(entries["Exec"_],' ',1,-1));
                for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries["Name"_]);
                arguments.filter([](const string& argument){return argument.contains('%');});
                shortcuts << Command(move(icon),string(entries["Name"_]),move(path),move(arguments));
            }
        }

        hourBox<<&rise<<&clock<<&set;
        timeBox<<&hourBox<<&calendar;
        hbox<<&feeds<<&timeBox<<&shortcuts;
        vbox<<&runner<<&hbox<<&weather;
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        weather.contentChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        browser.localShortcut(Escape).connect(&browser, &Window::destroy);
        browser.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { browser.destroy(); exit(); return; } // Exits application to free any memory leaks (will be restarted by .xinitrc)
        page.delta=0;
        page.contentChanged.connect(&browser, &Window::render);
        if(!browser.created) browser.create(); //might have been closed by user
        browser.setTitle(title);
        browser.setIcon(favicon);
        browser.setType("_NET_WM_WINDOW_TYPE_NORMAL"_);
        page.go(link);
        browser.widget=&page.area(); browser.show();
    }
} application;
