#include "display.h"
#include "window.h"
#include "font.h"
#include "text.h"
struct FontTest : Application, Widget {
    Image image[2]; int2 size __(5*4*16,2*4*16);
    Window window __(this,size,"Font Test"_);
    FontTest(){
        ref<byte> line =
                    "G"//"BDGPUI"
                    //"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    //"The quick brown fox jumps over the lazy dog\n"
                    //"I know that a lot of you are passionate about the civil war\n"
                    //"La Poste Mobile a gagné 4000 clients en six mois\n"
                    //"Fixed subpixel font layout (was broken by justification\n"
                    //"IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII"
                    ""_;
        for(int i=0;i<1;i++) {
            image[i]=Image(size.x,size.y);
            framebuffer=share(image[i]); currentClip=Rect(size);
            fill(Rect(size),white);
            extern int fit,nofilter,nodown,up,correct; extern map<int,Font> defaultSans;
            defaultSans.clear(); fit=0; nofilter=1; nodown=0; up=nodown?16:4; correct=i; Text(string(line)).render(int2(0,0*(up*16)),int2(size.x,0));
            defaultSans.clear(); fit=1; nofilter=1; nodown=0; up=nodown?16:4; correct=i; Text(string(line)).render(int2(0,1*(up*16)),int2(size.x,0));
        }
        if(!image[1]) image[1]=share(image[0]);
        window.localShortcut(Escape).connect(this,&Application::quit); window.bgCenter=window.bgOuter=0xFF; window.show();
    }
    bool toggle=false;
    void render(int2 position, int2) { blit(position,image[toggle]); }
    bool mouseEvent(int2, int2, Event event, MouseButton) { if(event==Enter) { toggle=true; return true;} if(event==Leave) {toggle=false; return true;} return false; }
};Application(FontTest)

/*#include "html.h"
struct HTMLTest : Application {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"Browser"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://feedproxy.google.com/~r/Phoronix/~3/tLzs7apkEps/vr.php"_);
        window.show();
    }
};Application(HTMLTest)*/

/*
#include "display.h"
struct VSyncTest : Application, Widget {
    Window window __(this,0,"VSyncTest"_);
    VSyncTest(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    void render(int2 position, int2 size) {static bool odd; fill(position+Rect(size),(odd=!odd)?black:white); window.render();}
};

struct KeyTest : Application, Text {
    Window window __(this,int2(640,480),"KeyTest"_);
    KeyTest(){ window.globalShortcut(Play).connect(this,&Application::quit); focus=this; window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    bool keyPress(Key key) { setText(str(dec(int(key)),hex(int(key)))); return true; }
};

#include "png.h"
struct PNGTest : Application, ImageView {
    Window window __(this,int2(16,16),"PNGTest"_);
    ICON(arrow) PNGTest(){
        image=decodeImage(encodePNG(arrowIcon()));
        window.localShortcut(Escape).connect(this,&Application::quit); window.show();
    }
};

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
        window.localShortcut(Escape).connect(this,&Application::quit); window.bgCenter=window.bgOuter=0xFF; window.show();
    }
    void renderPage() {
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        WeekView().render(int2(16,16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
        WeekView().render(int2(16,framebuffer.size().y/2+16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
    }
    void render(int2 position, int2 size) { blit(position,resize(page,page.width*size.y/page.height,size.y)); }
};

#include "calendar.h"
struct ClockTest : Application, Clock { Window window __(this,int2(-1,-1),"Clock"_); };
*/
