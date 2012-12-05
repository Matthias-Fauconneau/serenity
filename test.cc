#if 0
#include "window.h"
#include "text.h"
struct TextInputTest {
    TextInput input __(readFile("ternary.l"_,cwd()));
    Window window __(&input,int2(525,525),"TextInput"_);

    TextInputTest() {
        window.localShortcut(Escape).connect(&exit);
    }
} test;
#endif

#if 0
#include "window.h"
#include "display.h"
#include "text.h"

inline float acos(float t) { return __builtin_acosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }

/// Directional light with angular diameter
inline float angularLight(float dotNL, float angularDiameter) {
    float t = ::acos(dotNL); // angle between surface normal and light principal direction
    float a = min<float>(PI/2,max<float>(-PI/2,t-angularDiameter/2)); // lower bound of the light integral
    float b = min<float>(PI/2,max<float>(-PI/2,t+angularDiameter/2)); // upper bound of the light integral
    float R = sin(b) - sin(a); // evaluate integral on [a,b] of cos(t-dt)dt (lambert reflectance model) //TODO: Oren-Nayar
    R /= 2*sin(angularDiameter/2); // normalize
    return R;
}

struct Plot : Widget {
    Window window __(this,int2(0,1050),"Plot"_);
    Plot(){ window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF; }
    void render(int2, int2 size) {
        float Y[size.x+1]; float min=1, max=-1;
        for(int n=0;n<=size.x;n++) {
            float x = ((n-0.5)/size.x)*2-1;
            float y = angularLight(x,PI/2);
            Y[n]=y;
            min=::min(min,y);
            max=::max(max,y);
        }
        for(int x=0;x<size.x;x++) line(x-0.5,size.y-size.y*(Y[x]-min)/(max-min),x+0.5,size.y-size.y*(Y[x+1]-min)/(max-min));
        for(int y=0;y<size.y;y+=100) for(int x=0;x<size.x;x+=100) {
            Text(str(((x-0.5)/size.x)*2-1,min+(max-min)*y/size.y)).render(int2(x,size.y-y));
        }
    }
} test;

#endif

#if 0
#include "window.h"
#include "display.h"
#include "raster.h"

// Test correct top-left rasterization rules using a simple triangle fan
struct PolygonTest : Widget {
    Window window __(this,int2(4*64,6*64),"PolygonTest"_);
    PolygonTest(){
        window.localShortcut(Escape).connect(&exit);
        window.backgroundColor=window.backgroundCenter=0xFF;
    }
    void render(int2 position, int2 size) {
        RenderTarget target(4*64*4,6*64*4);
        RenderPass<vec4,1> pass(target,4);
        mat4 M; M.scale(64*4);
        pass.submit(M*vec3(2,3,0),M*vec3(1,1,0),M*vec3(3,1,0),(vec3[]){vec3(0,0,0)},vec4(0,0,0,1./2)); //bottom
        pass.submit(M*vec3(2,3,0),M*vec3(3,1,0),M*vec3(3,5,0),(vec3[]){vec3(0,0,0)},vec4(0,0,1,1./2)); // right
        pass.submit(M*vec3(2,3,0),M*vec3(3,5,0),M*vec3(1,5,0),(vec3[]){vec3(0,0,0)},vec4(0,1,0,1./2)); //top
        pass.submit(M*vec3(2,3,0),M*vec3(1,5,0),M*vec3(1,1,0),(vec3[]){vec3(0,0,0)},vec4(1,0,0,1./2)); //left
        function<vec4(vec4,float[0])> flat = [](vec4 color,float[0]){return color;};
        pass.render(flat);
        target.resolve(position,size);
    }
} test ;
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
#include "window.h"
#include "pdf.h"
#include "interface.h"
struct Book {
    string file;
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(0,0),"Book"_);
    Book() {
        if(arguments()) file=string(arguments().first());
        else if(existsFile("Books/.last"_)) {
            string mark = readFile("Books/.last"_);
            ref<byte> last = section(mark,0);
            if(existsFile(last)) {
                file = string(last);
                pdf.delta.y = toInteger(section(mark,0,1,2));
            }
        }
        pdf.open(readFile(file,root()));
        window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(UpArrow).connect(this,&Book::previous);
        window.localShortcut(LeftArrow).connect(this,&Book::previous);
        window.localShortcut(RightArrow).connect(this,&Book::next);
        window.localShortcut(DownArrow).connect(this,&Book::next);
        window.localShortcut(Power).connect(this,&Book::next);
    }
    void previous() { pdf.delta.y += window.size.y/2; window.render(); save(); }
    void next() { pdf.delta.y -= window.size.y/2; window.render(); save(); }
    void save() { writeFile("Books/.last"_,string(file+"\0"_+dec(pdf.delta.y))); }
} application;
#endif

#if 0
#include "process.h"
#include "data.h"
#include "string.h"
#include "display.h"
#include "text.h"
#include "widget.h"
#include "window.h"
#include "pdf.h" //mat32
#include "png.h"

struct Wing : Widget {
    int width = 2*1280, height = width/sqrt(2);
    Window window __(this,int2(1280,height/2),"Graph"_);
    struct Point : vec2 { Point(vec2 v, string&& label, float p):vec2(v),label(move(label)),p(p){} string label; float p; };
    struct Curve : array<Point> { vec2 min,mean,max; string name; float integral=0; };
    array< Curve > curves;
    Image page;
    Wing() {
        window.localShortcut(Escape).connect(&exit);
        window.backgroundColor=window.backgroundCenter=0xFF;

        parse("M1PAM/wing"_);

        Image page __(width,height);
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        render(int2(0,0),page.size());
        writeFile("plot.png"_,encodePNG(page),home());
        this->page=move(page);
    }
    void parse(const ref<byte>& path) {
        TextData text = readFile(path,home());
        typedef ref<byte> Field;
        map<ref<byte>, double> parameters;
        for(Field e: split(text.line(),'\t')) parameters[section(e,'=')]=toDecimal(section(e,'=',1,2));
        array<Field> headers = split(text.line(),'\t');
        int columns = headers.size();
        array< array<Field> > data; data.grow(columns);
        while(text) {
            array<Field> fields = split(text.line(),'\t');
            assert(fields.size()==headers.size(),fields);
            uint i=0; for(Field field: fields) data[i++] << field;
        }
        array<string> labels; for(Field label: data[0]) labels<< string(label);
        array<float> X; for(Field x: data[1]) X<< toDecimal(x);
        array<float> Y; for(Field y: data[2]) Y<< toDecimal(y);
        array<Curve> curves;
        for(uint a: range(3,data.size())) {
            Curve curve;
            curve.name = string(headers[a]);
            float angle = toDecimal(curve.name)*PI/180;
            mat32 M(cos(angle),-sin(angle),sin(angle),cos(angle),0,0);
            const array<Field>& H = data[a];
            for(uint i: range(H.size())) {
                float p = (toDecimal(H[i])-parameters["hS"_])/(parameters["hT"_]-parameters["hS"_]);
                vec2 pos(X[i],Y[i]);
                curve<< Point(M*pos, copy(labels[i]), p);
            }
            curves << move(curve);
        }
        vec2 min=0,max=0;
        for(Curve& curve: curves) {
            vec2 sum=0;
            for(vec2 p: curve) {
                if(p.x<min.x) min.x=p.x; else if(p.x>max.x) max.x=p.x;
                if(p.y<min.y) min.y=p.y; else if(p.y>max.y) max.y=p.y;
                sum += p;
            }
            curve.mean = sum/float(curve.size());
        }
        float delta = (max.x-min.x)-(max.y-min.y);
        min.y -= delta/2, max.y += delta/2;
        assert((max.x-min.x)==(max.y-min.y));
        for(Curve& curve: curves) {
            curve.min=min, curve.max=max;
            this->curves << move(curve);
        }
    }
    void render(int2 position, int2 size) {
        if(page) { blit(position,resize(page,size.x,page.height*size.x/page.width)); return; }
        int width = size.x/6, margin=width/6;
        int height = size.y/1;
        position+=int2(0,height/4);
        for(uint a: range(curves.size())) {
            int x = margin/2+a*(width+margin);
            Text(string("Pitch = "_+curves[a].name+"°"_),32).render(position+int2(x+width/2,0)+int2(-width/2,0),int2(width,0)); //column title
            plot(curves[a], position+int2(x,(height-width)/4), int2(width,width));
        }
    }
    void plot(const Curve& curve, int2 position, int2 size) {
        vec2 scale = vec2(size)/(curve.max-curve.min), offset = -curve.min*scale;
        mat32 M(scale.x,0,0,-scale.y,position.x+offset.x,position.y+size.y-offset.y); //[min..max] -> [position..position+size]
        const int N = curve.size();
        vec2 R=0; float t=0;
        for(uint i: range(N)) {
            //const Point& prev = curve[(i-1+N)%N];
            const Point& curr = curve[i];
            const Point& next = curve[(i+1)%N];
            float p = (curr.p+next.p)/2.f;
            vec2 F = -p*normal(next-curr); //-curr.p/2*normal(next-prev); //positive pression is towards inside => minus sign; each segment is used twice => /2
            vec2 x = (curr+next)/2.f;
            vec2 r = curr-curve.mean;
            t += cross(r,F);
            R += F;
            line(M*curr, M*next, 2);
            line(M*x, M*(x+F), 2, p>0?red:blue);
        }
        line(M*curve.mean, M*(curve.mean+R), 2);
        float drag = R.x;
        line(M*curve.mean, M*(curve.mean+vec2(drag,0)), 2, red); //drag
        float lift = R.y;
        line(M*curve.mean, M*(curve.mean+vec2(0,lift)), 2, green); //lift
        Text(str("Lift =",lift/100),                32).render(position+int2(0,size.y+0*32),int2(size.x,0));
        Text(str("Drag =",drag/100),          32).render(position+int2(0,size.y+1*32),int2(size.x,0));
        Text(str("Lift/Drag =",lift/drag),32).render(position+int2(0,size.y+2*32),int2(size.x,0));
        Text(str("Torque =",t),32).render(position+int2(0,size.y+3*32),int2(size.x,0));
    }
} application;
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

#if 1
#include "window.h"
#include "html.h"
struct HTMLTest {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"HTML"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(&exit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://feedproxy.google.com/~r/Phoronix/~3/LdcmrpZu6FA/vr.php"_);
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
            const int workWeek=4;
            int w = size.x/workWeek, y=0;
            for(int i=0;i<workWeek;i++) {
                constexpr ref<byte> days[5]={"Lundi"_,"Mardi"_,"Mercredi"_,"Jeudi"_,"Vendredi"_};
                Text day(string(days[i]),64);
                y=max(y,day.sizeHint().y);
                day.render(position+int2(i*w,0),int2(w,0));
            }
            array< ::Event> events = getEvents(Date());
            uint min=-1,max=0;
            for(const ::Event& e: events) if(e.date.day==-1 && e.date.weekDay!=-1 && e.end!=e.date) min=::min(min,floor(60,time(e.date))), max=::max(max,ceil(60,time(e.end)));
            for(const ::Event& e: events) if(e.date.day==-1 && e.date.weekDay!=-1 && e.end!=e.date) { // Displays only events recurring weekly
                int x = e.date.weekDay*w;
                int begin = y+(size.y-y)*(time(e.date)-min)/(max-min);
                fill(position+int2(x,begin-2)+Rect(int2(w,3)));
                int end = y+(size.y-y)*(time(e.end)-min)/(max-min);
                fill(position+int2(x,end-2)+Rect(int2(w,3)));
                fill(position+int2(x,begin)+Rect(int2(3,end-begin)));
                fill(position+int2(x+w-2,begin)+Rect(int2(3,end-begin)));
                Text time(str(e.date,"hh:mm"_)+(e.date!=e.end?string("-"_+str(e.end,"hh:mm"_)):string()),64);
                time.render(position+int2(x, begin+3),int2(w,0));
                Text title(copy(e.title),64);
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
