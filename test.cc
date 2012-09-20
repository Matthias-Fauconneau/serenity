#if 1
#include "window.h"
#include "pdf.h"
struct PDFTest : Application {
    PDF pdf __("3. The Mystery Knight.pdf"_,home());
    Window window __(&pdf,int2(-1,-1),"PDF Test"_);
    PDFTest(){ window.localShortcut(Escape).connect(this,&Application::quit); window.backgroundCenter=window.backgroundColor=0xFF; window.show(); }
};Application(PDFTest)
#endif

#if 0
#include "process.h"
#include "time.h"
#include "map.h"
/// Convenient partial template specialization to automatically copy ref<byte> keys
template<class V> struct map<ref<byte>,V> : map<string,V> {
    V& operator [](ref<byte> key) { return map<string,V>::operator[](string(key)); }
};
struct MonitorTest : Application, Timer {
    Folder procfs __("proc"_);
    /// Returns system statistics
    map<ref<byte>,string> stat() {
        string stat = File("stat"_,procfs).readUpTo(4096);
        const ref<byte> keys[]={""_,"user"_, "nice"_, "idle"_};
        array< ref<byte> > fields = split(stat,' ');
        map< ref<byte>, string> stats;
        for(uint i=0;i<3;i++) stats[keys[i]]=string(fields[i]);
        return stats;
    }
    /// Returns process statistics
    map<ref<byte>, string> stat(const ref<byte>& pid) {
        const ref<byte> keys[]={"pid"_, "name"_, "state"_, "parent"_, "group"_, "session"_, "tty"_, "tpgid"_, "flags"_, "minflt"_, "cminflt"_, "majflt"_, "cmajflt"_, "utime"_, "stime"_, "cutime"_, "cstime"_, "priority"_, "nice"_, "#threads"_, "itrealvalue"_, "starttime"_, "vsize"_, "rss"_};
        string stat = File("stat"_,Folder(pid,procfs)).readUpTo(4096);
        array< ref<byte> > fields = split(stat,' ');
        map< ref<byte>, string> stats;
        for(uint i=0;i<24;i++) stats[string(keys[i])]=string(fields[i]);
        return stats;
    }
    map<ref<byte>, string> system;
    map<ref<byte>, map<ref<byte>,string> > process;
    MonitorTest() { event(); }
    void event() {
        map<ref<byte>, uint> memory;
        for(TextData s = File("/proc/meminfo"_).readUpTo(4096);s;) {
            ref<byte> key=s.until(':'); s.skip();
            uint value=toInteger(s.untilAny(" \n"_)); s.until('\n');
            memory[key]=value;
        }
        map<ref<byte>, string>& o = this->system;
        map<ref<byte>, string> n = stat();
        map<ref<byte>, map<ref<byte>, string> > process;
        for(const string& pid: listFiles(""_,Folders,procfs)) if(isInteger(pid)) process[pid]=stat(pid);
        if(o) {
            log("User: "_+dec(toInteger(n["user"_])-toInteger(o["user"_]))+"%"
                "\tNice: "_+dec(toInteger(n["nice"_])-toInteger(o["nice"_]))+"%"
                "\tIdle: "_+dec(toInteger(n["idle"_])-toInteger(o["idle"_]))+"%"
                "\tFree Memory: "_+dec((memory["MemFree"_]+memory["Inactive"_])/1024)+" MB\tDisk Buffer: "_+dec(memory["Active(file)"_]/1024)+" MB"_);
            log("Name\tState\tRSS (kB)\tCPU (%)");
            for(string& pid: listFiles(""_,Folders,procfs)) if(isInteger(pid)) {
                map<ref<byte>,string>& o = this->process[pid];
                map<ref<byte>,string>& p = process[pid];
                int cpu = toInteger(p["utime"_])-toInteger(o["utime"_])+toInteger(p["stime"_])-toInteger(o["stime"_]);
                const ref<byte> state[]={"Running"_, "Sleeping"_, "Waiting for disk"_, "Zombie"_, "Traced/Stopped"_,  "Paging"_};
                if(p["state"_]=="R"_) log(p["name"_].slice(1,p["name"_].size()-2)+"\t"_+state["RSDZTW"_.indexOf(p["state"_][0])]+"\t"_+dec(toInteger(p["rss"_])/4)+"\t"_+dec(cpu));
            }
        }
        this->system=move(n); this->process=move(process);
        setAbsolute(currentTime()+1);
    }
};Application(MonitorTest)
#endif

#if 0
#include "window.h"
#include "html.h"
struct HTMLTest : Application {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"Browser"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://www.girlgeniusonline.com/comic.php?date=20120917"_);
        window.show();
    }
};Application(HTMLTest)
#endif

#if 0
#include "display.h"
struct VSyncTest : Application, Widget {
    Window window __(this,0,"VSyncTest"_);
    VSyncTest(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    void render(int2 position, int2 size) {static bool odd; fill(position+Rect(size),(odd=!odd)?black:white); window.render();}
};
#endif

#if 0
struct KeyTest : Application, Text {
    Window window __(this,int2(640,480),"KeyTest"_);
    KeyTest(){ window.globalShortcut(Play).connect(this,&Application::quit); focus=this; window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    bool keyPress(Key key) { setText(str(dec(int(key)),hex(int(key)))); return true; }
};
#endif

#if 0
#include "calendar.h"
struct WeekViewTest : Application, Widget {
    struct WeekView : Widget {
        uint time(Date date) { return date.hours*60+date.minutes; }
        inline uint floor(uint width, uint value) { return value/width*width; }
        inline uint ceil(uint width, uint value) { return (value+width-1)/width*width; }
        void render(int2 position, int2 size) {
            int w = size.x/5, y=0;
            for(int i=0;i<5;i++) {
                constexpr ref<byte> days[5]={"Lundi"_,"Mardi"_,"Mercredi"_,"Jeudi"_,"Vendredi"_};
                Text day(string(days[i]),64);
                y=max(y,day.sizeHint().y);
                day.render(position+int2(i*w,0),int2(w,0));
            }
            array< ::Event> events = getEvents();
            uint min=-1,max=0;
            for(::Event& e: events) min=::min(min,floor(60,time(e.date))), max=::max(max,ceil(60,time(e.end)));
            for(::Event& e: events) {
                int x = e.date.weekDay*w;
                int begin = y+(size.y-y)*(time(e.date)-min)/(max-min);
                fill(position+int2(x,begin-2)+Rect(int2(w,3)));
                int end = y+(size.y-y)*(time(e.end)-min)/(max-min);
                fill(position+int2(x,end-2)+Rect(int2(w,3)));
                fill(position+int2(x,begin)+Rect(int2(3,end-begin)));
                fill(position+int2(x+w-2,begin)+Rect(int2(3,end-begin)));
                Text time(str(e.date,"hh:mm"_)+(e.date!=e.end?string("-"_+str(e.end,"hh:mm"_)):string()),64);
                time.render(position+int2(x, begin+3),int2(w,0));
                Text title(move(e.title),64);
                title.render(position+int2(x, begin),int2(w,end-begin));
            }
        }
    };

    Window window __(this,0,"WeekView"_);
    Image page __(2480,3508);
    WeekViewTest(){
        renderPage();
        //writeFile(string(getenv("HOME"_)+"/week.png"_),encodePNG(page));
        window.localShortcut(Escape).connect(this,&Application::quit); window.backgroundCenter=window.backgroundColor=0xFF; window.show();
    }
    void renderPage() {
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        WeekView().render(int2(16,16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
        WeekView().render(int2(16,framebuffer.size().y/2+16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
    }
    void render(int2 position, int2 size) { blit(position,resize(page,page.width*size.y/page.height,size.y)); }
};
#endif
