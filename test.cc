#if 1
#include "process.h"
#include "data.h"
#include "string.h"
#include "display.h"
#include "text.h"
#include "widget.h"
#include "window.h"
#include "pdf.h" //mat32

struct Wing : Widget {
    Window window __(this,int2(5*256,2*256),"Graph"_);
    struct Point : vec2 { Point(vec2 p, string&& label, int sign):vec2(p),label(move(label)),sign(sign){} string label; int sign; };
    struct Curve : array<Point> { vec2 min,max; string name; double integral; };
    array< Curve > curves;
    Wing() {
        window.localShortcut(Escape).connect(&exit);
        window.backgroundColor=window.backgroundCenter=0xFF;

        parse("M1PAM/lift"_);
        parse("M1PAM/drag"_);
    }
    void parse(const ref<byte>& path) {
        TextData text = readFile(path,home());
        typedef ref<byte> Field;
        array<Field> headers = split(text.line(),'\t');
        int columns = headers.size();
        array< array<Field> > data; data.grow(columns);
        while(text) {
            array<Field> fields = split(text.line(),'\t');
            assert(fields.size()==headers.size(),fields);
            uint i=0; for(Field field: fields) data[i++] << field;
        }
        array<string> labels; for(Field label: data[0]) labels<< string(label);
        array<int> signs; for(Field sign: data[1]) signs<< (sign=="+"_? 1 : -1);
        array<double> X; for(Field x: data[2]) X<< toDecimal(x);
        array<Curve> curves;
        for(const array<Field>& Y: data.slice(3)) {
            Curve curve;
            for(uint i: range(Y.size())) {
                double y = toDecimal(Y[i]);
                curve<< Point(vec2(X[i],y), copy(labels[i]), signs[i]);
            }
            curves << move(curve);
        }
        vec2 min=0,max=0;
        for(const Curve& points: curves) {
            for(vec2 p: points) {
                if(isNaN(p)) continue;
                if(p.x<min.x) min.x=p.x; else if(p.x>max.x) max.x=p.x;
                if(p.y<min.y) min.y=p.y; else if(p.y>max.y) max.y=p.y;
            }
        }
        for(Curve& curve: curves) {
            curve.min=min, curve.max=max;
            /// Compute numeric integral (linear order)
            double integral=0;
            uint last=0;
            for(uint i: range(1,curve.size())) {
                const Point &a = curve[last], &b=curve[i];
                if(isNaN(b)) continue;
                /*float width = b.x-a.x;
                integral += a
                last=i;*/
            }
            this->curves << move(curve);
        }
    }
    void render(int2 position, int2 size) {
        for(uint a: range(curves.size())) plot(curves[a],position+int2((a%5)*size.x/5,a/5*(size.y/2)),int2(size.x/5,size.y/2));
    }
    void plot(const Curve& curve, int2 position, int2 size) {
        vec2 scale = vec2(size)/(curve.max-curve.min), offset = -curve.min*scale;
        mat32 M(scale.x,0,0,-scale.y,position.x+offset.x,position.y+size.y-offset.y); //[min..max] -> [position..position+size]
        line(M*vec2(curve.min.x,0),M*vec2(curve.max.x,0));
        line(M*vec2(0,curve.min.y),M*vec2(0,curve.max.y));
        uint last=0;
        for(uint i: range(1,curve.size())) {
            const Point &a = curve[last], &b=curve[i];
            if(isNaN(b)) continue;
            if(a.x!=b.x) line(M*a,M*b, b.x>a.x?red:blue);
            Text(copy(b.label),10).render(int2(M*b)+int2(-8,b.sign==1?-16:0),int2(16,16));
            last=i;
        }
    }
} application;
#endif

#if 0
#include "linux.h"
int main() { write(1,"Hello World!\n",sizeof("Hello World!\n")-1); return 0; }
#endif

#if 0
#include "window.h"
#include "pdf.h"
#include "interface.h"
struct PDFTest {
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(-1,-1),"PDF Test"_);
    PDFTest(){
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        pdf.open("/Sheets/Moonlight Sonata.pdf"_,home());
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
