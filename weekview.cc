#include "window.h"
#include "calendar.h"
#include "png.h"

struct WeekViewPrint : Widget {
    struct WeekView : Widget {
        uint time(Date date) { assert(date.hours>=0 && date.minutes>=0, date); return date.hours*60+date.minutes; }
        inline uint floor(uint width, uint value) { return value/width*width; }
        inline uint ceil(uint width, uint value) { return (value+width-1)/width*width; }
        void render(int2 position, int2 size) {
            const int workWeek=5;
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

    Window window {this,int2(0,0),"WeekView"};
    Image page {2480,3508};
    WeekViewPrint(){
        renderPage();
        writeFile("week.png"_,encodePNG(page),home());
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=1;
    }
    void renderPage() {
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        WeekView().render(int2(16,16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
        WeekView().render(int2(16,framebuffer.size().y/2+16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
    }
    void render(int2 position, int2 size) { blit(position,resize(page,size.x,page.height*size.x/page.width)); }
} application;
