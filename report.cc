#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"

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

    TextData s;

    array<unique<Widget>> elements;

    int pageIndex=0;

    array<uint> levels;
    struct Entry { array<uint> levels; String name; uint page; };
    array<Entry> tableOfContents;

    Document(string source) : s(filter(source, [](char c) { return c=='\r'; })) {
        while(s) { layoutPage(Image()); pageIndex++; } // Generates table of contents
    }

    template<Type T, Type... Args> T& element(Args&&... args) {
        unique<T> t(forward<Args>(args)...);
        T* pointer = t.pointer;
        elements << unique<Widget>(move(t));
        return *pointer;
    }
    Text& newText(string text, int size=textSize) { return element<Text>(text, size, 0, 1, width, font, 3./2); }

    void layoutPage(const Image& target) {
        elements.clear();
        VBox page (Linear::Center, Linear::Expand);

        uint emptyLine = 0;
        while(s) {
            if(s.match('\n')) {
                emptyLine++;
                continue;
            } else {
                if(emptyLine >= 2) break; // Page break
                emptyLine = 0;
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
            }

            uint level = 0;
            while(s.match('#')) level++;

            /*if(s.match('*')) {
                assert_(!bold);
                bold = true;
            }*/

            if(s.match('\\')) {
                string command = s.line();
                if(command == "tableOfContents"_) {
                    auto& vbox = element<VBox>(Linear::Share, Linear::Expand);
                    for(const Entry& entry: tableOfContents) {
                        String header;
                        header << repeat(" "_, entry.levels.size);
                        if(entry.levels.size<=1) header << format(TextFormat::Bold);
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

            String text = String( trim( s.line() ) );

            if(level) {
                if(level > levels.size) levels.grow(level);
                if(level < levels.size) levels.shrink(level);
                levels[level-1]++;
                if(!target) tableOfContents << Entry{copy(levels), copy(text), (uint)pageIndex};
                bold = true;
            }
            if(bold) text = format(TextFormat::Bold) + text;

            page << &newText(text, size);
        }
        if(target) {
            fill(target, Rect(target.size()), white);
            page.Widget::render(target);
        }
    }

    int viewPageIndex = 0;

    bool keyPress(Key key, Modifiers) {
        /**/ if(key == LeftArrow) viewPageIndex = max(0, viewPageIndex-1);
        else if(key == RightArrow) viewPageIndex++;
        else return false;
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        /**/ if(button==WheelUp) viewPageIndex = max(0, viewPageIndex-1);
        else if(button==WheelDown) viewPageIndex++;
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
    FileWatcher(string path, function<void(string)> fileModified)
        : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), watch(check(inotify_add_watch(File::fd, strz(path), IN_MODIFY))), fileModified(fileModified) {}
    void event() override {
        while(poll()) {
            ::buffer<byte> buffer = readUpTo(sizeof(struct inotify_event) + 256);
            inotify_event e = *(inotify_event*)buffer.data;
            string name = str((const char*)buffer.slice(__builtin_offsetof(inotify_event, name), e.len-1).data);
            assert_(e.mask&IN_MODIFY);
            fileModified(name);
        }
    }
    const uint watch;
    function<void(string)> fileModified;
};

struct Report {
    String path = homePath()+"/Rapport/rapport.txt"_;
    Document document {readFile(path, home())};
    Window window {&document, int2(1*document.width/document.oversample,document.height/document.oversample), "Report"_};
    FileWatcher watcher{path, [this](string){ delete &document; new (&document) Document(readFile(path, home()));/*Reloads*/ window.render(); } };
    Report() {
        window.actions[Escape]=[]{exit();}; window.background=White; window.focus = &document; window.show();
    }

} app;

