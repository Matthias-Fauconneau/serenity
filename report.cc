#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
#include "png.h"
//#include "jpeg.h"

struct Document : Widget {
    const int2 windowSize = int2(532, 752);
    static constexpr int oversample = 4;
    const int2 previewSize = oversample * windowSize;

    static constexpr bool showMargins = true;
    const float margin = 1./10;
    const int2 pageSize = oversample * int2(round(vec2(windowSize) * (1+(showMargins?0:2*margin))));
    const int2 contentSize = oversample * int2(round(vec2(windowSize) * (1-(showMargins?2*margin:0))));

    static constexpr float pageHeightMM = 297;
    static constexpr float pointMM = 0.3527;
    const float point = pointMM * (pageSize.y * (1+2*margin)) / pageHeightMM;
    const float textSize = 12 * point;
    const float headerSize = 14 * point;
    const float titleSize = 16 * point;
    static constexpr float inchMM = 25.4;
    //static_assert(pageHeight / (pageHeightMM / inchMM) > 300, ""); // 308.65

    const string font = "FreeSerif"_;
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
    Text& newText(string text, int size, bool center=true) { return element<Text>(text, size, 0, 1, contentSize.x, font, interlineStretch, center); }

    String parseLine(TextData& s) {
        String text;
        while(s && !s.match('\n')) { // Line
            /**/ if(s.match('*')) text << (char)(TextFormat::Bold);
            else if(s.match("//"_)) text << "/"_;
            else if(s.match('/')) text << (char)(TextFormat::Italic);
            //else if(s.match('_')) userText << (char)(TextFormat::Underline);
            else text << s.next();
        }
        return text;
    }

    String parseParagraph(TextData& s) {
        String text;
        while(s && !s.match('\n')) text << parseLine(s) << ' ';
        return text;
    }

    Widget* parseLayout(TextData& s) {
        array<Widget*> children;
        char type = 0;
        while(!s.match(')')) {
            // Element
            s.whileAny(" \n"_);
            if(s.match('(')) children << parseLayout(s);
            else {
                string text = trim(s.whileNo("|-+)"_));
                assert_(text, s.line());
                if(startsWith(text,"&"_)) {
                    string path = text.slice(1);
                    /**/ if(existsFile(path+".png"_)) images << unique<Image>(decodeImage(readFile(path+".png"_)));
                    else if(existsFile(path+".jpg"_)) images << unique<Image>(decodeImage(readFile(path+".jpg"_)));
                    else error("Missing image", path);
                    children << &element<ImageWidget>(images.last());
                } else {
                    children << &newText(text, textSize);
                }
            }
            // Separator
            s.whileAny(" \n"_);
            /**/ if(s.match(')')) break;
            else if(!type) type = s.next();
            else s.skip(string(&type,1));
        }
        if(type=='-') return &element<VBox>(move(children));
        else if(type=='|') return &element<HBox>(move(children));
        else if(type=='+') return &element<WidgetGrid>(move(children));
        else { error(type, int(type), s.line()); assert_(children.size==1); return children.first(); }
    }

    void layoutPage(const Image& target) {
        elements.clear();
        images.clear();
        VBox page (Linear::Center, Linear::Expand);

        while(s) {
            if(s.match('%')) { // Comment
                s.line();
                continue;
            }

            if(s.match('(')) { // Float
                page << parseLayout(s);
                s.skip("\n"_);
                continue;
            }

            if(s.match('-')) { // List
                VBox& list = element<VBox>(VBox::Even);
                do {
                    list << &newText("- "_+parseLine(s), textSize, false);
                } while(s.match('-'));
                page << &list;
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
                        hbox << &newText(header, textSize);
                        hbox << &newText(" "_+dec(entry.page), textSize);
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

            String userText = parseParagraph(s);
            if(userText) {
                text << trim(userText);
                if(level && !target) tableOfContents << Entry{copy(levels), move(userText), (uint)pageIndex};
                page << &newText(text, size, center);
            }
            if(s.match('\n')) {
                //if(pageIndex>0) page << &newText(dec(pageIndex)); //FIXME: should be outside content box
                break; // Page break
            }
        }
        if(target) {
            fill(target, Rect(target.size()), white);
            if(showMargins) {
                int2 margin = int2(round(this->margin * vec2(target.size())));
                Image inner = clip(target, Rect(margin, target.size()-margin));
                page.Widget::render(inner);
                Image footer = clip(target, Rect(int2(0,target.size().y-margin.y),target.size()));
                Text(dec(pageIndex), textSize, 0, 1, 0, font).Widget::render(footer);
            } else {
                page.Widget::render(target);
            }
        }
    }

    int viewPageIndex = 0; //FIXME: persistent
    signal<int> pageChanged;

    bool keyPress(Key key, Modifiers) {
        /**/ if(key == LeftArrow) viewPageIndex = max(0, viewPageIndex-1);
        else if(key == RightArrow) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        pageChanged(viewPageIndex);
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        setFocus(this);
        /**/ if(button==WheelUp) viewPageIndex = max(0, viewPageIndex-1);
        else if(button==WheelDown) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        pageChanged(viewPageIndex);
        return true;
    }

    void clear() {
        s.index = 0;
        pageIndex=0;
        elements.clear();
        levels.clear();
    }
    void render() {
        clear();
        while(s && pageIndex < viewPageIndex) {
            assert_( s.until("\n\n\n"_) );
            pageIndex++;
        }
        Image target (pageSize);
        renderBackground(target, White);
        while(s) {
            if(pageIndex > viewPageIndex) break;
            layoutPage(target);
            //layoutPage(target); // FIXME: auto page break with quick layout for previous pages
            //if(pageIndex < viewPageIndex) { pageIndex++; continue; }
            Image image = clip(this->target, int2((pageIndex-viewPageIndex)*(target.size().x/oversample),0)+Rect(target.size()/oversample));
            if(oversample==1) copy(image, target);
            else if(oversample>=2) {
                for(int oversample=2; oversample<this->oversample; oversample*=2) target=downsample(target);
                downsample(image, target);
            }
            pageIndex++;
        }
    }

    array<Image> renderPages() {
        clear();
        array<Image> pages;
        while(s) {
            Image target (pageSize);
            layoutPage(target);
            pages << move(target);
            pageIndex++;
        }
        return pages;
    }

    void writePages() {
        clear();
        array<String> args;
        while(s) {
            Image target (pageSize);
            layoutPage(target);
            log(pageIndex);
            writeFile(dec(pageIndex),encodePNG(target));
            args << dec(pageIndex);
            pageIndex++;
        }
        args << String("rapport.pdf"_);
        log("PDF");
        execute(which("convert"_),toRefs(args));
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
    Window window {&document, document.windowSize, "Report"_};
    FileWatcher watcher{path, [this](string){
            int index = document.viewPageIndex;
            document.~Document(); new (&document) Document(readFile(path)); // Reloads
            document.viewPageIndex = index;
            document.pageChanged.connect([&](int pageIndex){ window.setTitle(dec(pageIndex)); });
            window.render();
        } };
    Report() {
        assert_(arguments());
        /**/ if(arguments()[0]=="export"_) {
            document.writePages();
            exit(0);
            return;
        }
        else if(arguments()[0]=="preview"_) {
        }
        else error(arguments());
        window.actions[Escape]=[]{exit();}; window.background=White; window.focus = &document; window.show();
        document.pageChanged.connect([&](int pageIndex){ window.setTitle(dec(pageIndex)); });
    }

} app;

