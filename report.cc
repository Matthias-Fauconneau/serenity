#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
#include "png.h"

struct Document : Widget {
    static constexpr int oversample = 2;
    //static constexpr int height = oversample * (752*3/4); // 1/4 vertical margin
    //static constexpr int width = oversample* (532*3/4); // 1/4 horizontal margin
    static constexpr int height = oversample * 752;
    static constexpr int pageHeight = oversample * 752 * 4/3; // 1/4 uniform margin
    const int width = oversample * 532;
    static constexpr float pageHeightMM = 297;
    static constexpr float pointMM = 0.3527;
    static constexpr float point = pointMM * pageHeight / pageHeightMM;
    static constexpr float textSize = 12 * point;
    static constexpr float headerSize = 14 * point;
    static constexpr float titleSize = 16 * point;
    const string font = "Computer Modern"_;
    const float interlineStretch = 1; //4./3

    TextData s;

    int pageIndex=0, pageCount;

    array<unique<Widget>> elements;
    array<unique<Image>> images;

    array<uint> levels;
    struct Entry { array<uint> levels; String name; uint page; };
    array<Entry> tableOfContents;

    Document(string source) : s(filter(source, [](char c) { return c=='\r'; })) {
        while(s) { layoutPage(Image()); pageIndex++; } // Generates table of contents
        pageCount=pageIndex;
    }

    template<Type T, Type... Args> T& element(Args&&... args) {
        unique<T> t(forward<Args>(args)...);
        T* pointer = t.pointer;
        elements << unique<Widget>(move(t));
        return *pointer;
    }
    Text& newText(string text, int size=textSize) { return element<Text>(text, size, 0, 1, width, font, interlineStretch); }

    Widget* parseLayout(TextData& s) {
        array<Widget*> children;
        char type = 0;
        while(!s.match(')')) {
            // Element
            s.whileAny(" "_);
            if(s.match('(')) children << parseLayout(s);
            else {
                string text = trim(s.whileNo("|-)"_));
                assert_(text, s.line());
                if(startsWith(text,"&"_)) {
                    string path = text.slice(1);
                    images << unique<Image>(decodeImage(readFile(path+".png"_)));
                    children << &element<ImageWidget>(images.last());
                } else {
                    children << &newText(text);
                }
            }
            // Separator
            s.whileAny(" "_);
            /**/ if(s.match(')')) break;
            else if(!type) type = s.next();
            else s.skip(string(&type,1));
        }
        if(type=='-') return &element<VBox>(move(children));
        else if(type=='|') return &element<HBox>(move(children));
        else { error(type); assert_(children.size==1); return children.first(); }
    }

    void layoutPage(const Image& target) {
        elements.clear();
        VBox page (Linear::Center, Linear::Expand);

        while(s) {
            if(s.match('(')) { // Float
                page << parseLayout(s);
                s.skip("\n"_);
                continue;
            }

            bool center=false;
            if(s.match(' ')) {
                assert_(!center);
                center = true;
            }

            int size = textSize;
            bool bold = false;
            if(s.match('!')) {
                assert_(!bold);
                bold = true;
                size = titleSize;
                center = true;
            }

            uint level = 0;
            while(s.match('#')) {
                level++;
                center=true;
            }

            if(s.match('\\')) {
                string command = s.whileNot('\n');
                if(command == "tableOfContents"_) {
                    auto& vbox = element<VBox>(Linear::Top, Linear::Expand);
                    for(const Entry& entry: tableOfContents) {
                        String header;
                        header << repeat(" "_, entry.levels.size);
                        if(entry.levels.size<=1) header << (char)(TextFormat::Bold);
                        for(int level: entry.levels) header << dec(level) << '.';
                        header << ' ' << entry.name;
                        auto& hbox = element<HBox>(Linear::Spread);
                        hbox << &newText(header);
                        hbox << &newText(" "_+dec(entry.page));
                        vbox << &hbox;
                    }
                    page << &vbox;
                } else error(command);
                continue;
            }

            String text = center ? String(""_) : repeat(" "_, 4);

            if(level) {
                if(level > levels.size) levels.grow(level);
                if(level < levels.size) levels.shrink(level);
                levels[level-1]++;
                /*if(level==1)*/ bold = true;
            }

            if(bold) text << (char)(TextFormat::Bold);
            if(level) {
                for(int level: levels) text << dec(level) << '.';
                text << ' ';
            }

            String userText;
            while(s && !s.match('\n')) { // Paragraph
                while(s && !s.match('\n')) { // Line
                    /**/ if(s.match('*')) userText << (char)(TextFormat::Bold);
                    else if(s.match('/')) userText << (char)(TextFormat::Italic);
                    //else if(s.match('_')) userText << (char)(TextFormat::Underline);
                    else userText << s.next();
                }
                userText << " "_;
            }
            text << userText;
            if(level && !target) tableOfContents << Entry{copy(levels), move(userText), (uint)pageIndex};

            page << &newText(text, size);

            if(s.match('\n')) break; // Page break
        }
        if(target) {
            fill(target, Rect(target.size()), white);
            page.Widget::render(target);
        }
    }

    int viewPageIndex = 2; //FIXME: persistent

    bool keyPress(Key key, Modifiers) {
        /**/ if(key == LeftArrow) viewPageIndex = max(0, viewPageIndex-1);
        else if(key == RightArrow) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        setFocus(this);
        /**/ if(button==WheelUp) viewPageIndex = max(0, viewPageIndex-1);
        else if(button==WheelDown) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        return true;
    }

    void render() {
        s.index = 0;
        pageIndex=0;
        elements.clear();
        levels.clear();
        Image target (width, height);
        while(s) {
            if(pageIndex >= viewPageIndex+1) break;
            layoutPage(target);
            if(pageIndex < viewPageIndex) { pageIndex++; continue; }
            Image image = clip(this->target, int2((pageIndex-viewPageIndex)*(target.size().x/oversample),0)+Rect(target.size()/oversample));
            if(oversample==1) copy(image, target);
            else if(oversample==2) downsample(image, target);
            else error(oversample);
            pageIndex++;
        }
    }
};

// -> file.cc
#include <sys/inotify.h>
/// Watches a folder for new files
struct FileWatcher : File, Poll {
    string path;
    /*const*/ uint watch;
    function<void(string)> fileModified;

    FileWatcher(string path, function<void(string)> fileModified)
        : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), path(path), watch(check(inotify_add_watch(File::fd, strz(path), IN_MODIFY))), fileModified(fileModified) {}
    void event() override {
        while(poll()) {
            ::buffer<byte> buffer = readUpTo(sizeof(struct inotify_event) + 256);
            inotify_event e = *(inotify_event*)buffer.data;
            fileModified(e.len ? string(e.name, e.len-1) : string());
            inotify_rm_watch(File::fd, watch);
            watch = check(inotify_add_watch(File::fd, strz(path), IN_MODIFY));
        }
    }
};

struct Report {
    string path = "rapport.txt"_;
    Document document {readFile(path)};
    Window window {&document, int2(1*document.width/document.oversample,document.height/document.oversample), "Report"_};
    FileWatcher watcher{path, [this](string){
            int index = document.viewPageIndex;
            document.~Document(); new (&document) Document(readFile(path)); // Reloads
            document.viewPageIndex = index;
            window.render();
        } };
    Report() {
        window.actions[Escape]=[]{exit();}; window.background=White; window.focus = &document; window.show();
    }

} app;

