#if 1
#include "window.h"
#include "pdf.h"
#include "interface.h"
struct PDFTest {
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(-1,-1),"PDF Test"_);
    PDFTest(){
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        pdf.open("/Sheets/Brave Adventurers.pdf"_,home());
        window.setSize(int2(-1,-1));
    }
} test;
#endif

#if 0
#include "process.h"
#include "window.h"
#include "interface.h"
#include "png.h"
struct SnapshotTest : TriggerButton {
    Window window __(this,0,"SnapshotTest"_);
    SnapshotTest(){ writeFile("snapshot.png"_,encodePNG(window.snapshot()),home()); exit();}
} test;
#endif

#if 0
#include "process.h"
#include "window.h"
#include "text.h"
struct KeyTest : Text {
    Window window __(this,int2(640,480),"KeyTest"_);
    KeyTest(){ focus=this; window.localShortcut(Escape).connect(&exit); }
    bool keyPress(Key key) { setText(str("'"_+str((char)key)+"'"_,dec(int(key)),"0x"_+hex(int(key)))); return true; }
} test;
#endif

#if 0
#include "process.h"
#include "widget.h"
#include "display.h"
#include "window.h"
struct VSyncTest : Widget {
    Window window __(this,0,"VSync"_);
    VSyncTest(){ window.localShortcut(Escape).connect(&exit); }
    bool odd; void render(int2 position, int2 size) {fill(position+Rect(size),(odd=!odd)?black:white); window.render();}
} test;
#endif

#if 0
#include "window.h"
#include "interface.h"
#include "ico.h"
struct ImageTest : ImageView {
    Window window __(this,0,"Image"_);

    ImageTest():ImageView(resize(decodeImage(readFile("feedproxy.google.com/favicon.ico"_,cache())),16,16)) {
        assert(image.own);
        window.localShortcut(Escape).connect(&exit);
    }
} test;
#endif

#if 0
#include "window.h"
#include "html.h"
struct HTMLTest {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"HTML"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(&exit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://www.girlgeniusonline.com/comic.php?date=20120917"_);
    }
} test;
#endif

#if 0
#include "window.h"
#include "calendar.h"
#include "png.h"
struct WeekViewTest : Widget {
    struct WeekView : Widget {
        uint time(Date date) { assert(date.hours>=0 && date.minutes>=0, date); return date.hours*60+date.minutes; }
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
            array< ::Event> events = getEvents(Date());
            uint min=-1,max=0;
            for(::Event& e: events) if(e.date.day==-1) min=::min(min,floor(60,time(e.date))), max=::max(max,ceil(60,time(e.end)));
            for(::Event& e: events) if(e.date.day==-1) { // Displays only events recurring weekly
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
        writeFile("week.png"_,encodePNG(page),home());
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
    }
    void renderPage() {
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        WeekView().render(int2(16,16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
        WeekView().render(int2(16,framebuffer.size().y/2+16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
    }
    void render(int2 position, int2 unused size) { blit(position,resize(page,page.width/2,page.height/2)); }
} test;
#endif
