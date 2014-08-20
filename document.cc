#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
//#include "png.h"
//#include "jpeg.h"
//include "pdf.h"

/// Header of a section from a \a Document
struct Header {
    array<uint> indices; // Header index for each level
    String name; // Header name
    uint page; // Header page index
};

/// \a Page from a \a Document
struct Page : VBox {
    uint index;
    float marginPx;
    array<Header> headers;
    array<unique<Widget>> elements;
    array<unique<Image>> images;
    Widget* footer = 0;

    Page(uint index, float marginPx)
        : VBox(index ? Linear::Share : Linear::Center, Linear::Expand), index(index), marginPx(marginPx) {}

    void render() override {
        Image page = share(this->target);
        Image inner = clip(page, Rect(int2(marginPx,marginPx), page.size() - int2(marginPx,marginPx)));
        //fill(target, Rect(target.size()), !(sizeHint().y <= inner.size().y) ? vec3(3./4,3./4,1) : white);
       {this->target = share(inner);
            VBox::render();
        this->target = share(page);}
        Image footer = clip(page, Rect(int2(0, page.size().y - marginPx), page.size()));
        if(this->footer) this->footer->render(footer);
    }
};

/// Layouts a document
struct Document {
    // Page properties
    const int2 pageSize = int2(1050, 1485);
    const float pageHeightMM = 297;
    const float inchMM = 25.4;
    const float pageHeightInch = pageHeightMM / inchMM;
    const float inchPx = pageSize.y / pageHeightInch;
    const float pointPx = inchPx / 72;
    const float marginPx = 1.5 * inchPx;
    const int2 contentSize = pageSize - int2(2*marginPx);

    // Font properties
    const string font = "FreeSerif"_;
    const float interlineStretch = 3./2;
    const float textSize = 12 * pointPx;
    const float headerSize = 14 * pointPx;
    const float titleSize = 16 * pointPx;

    // Document properties
    const String source;
    array<Header> headers;
    array<string> pages; // Source slices for each page
    array<array<uint>> indices; // Level counters state at start of each page for correct single page rendering

    /// Generates page starts table, table of contents
    Document(String&& source_) : source(move(source_)) {
        assert_(!source.contains('\r')); //filter(source, [](char c) { return c=='\r'; }
        array<uint> indices;
        for(TextData s (source); s;) {
            uint start = s.index;
            this->indices << copy(indices);
            Page page = parsePage(s, indices, pages.size, true);
            pages << source(start, s.index);
            headers << move(page.headers);
        }
    }

    /// Registers a new element and returns it
    template<Type T, Type... Args> T& element(Page& page, Args&&... args) const {
        unique<T> t(forward<Args>(args)...);
        T* pointer = t.pointer;
        page.elements << unique<Widget>(move(t));
        return *pointer;
    }
    /// Registers a new text element and returns it
    Text& newText(Page& page, string text, int size, bool center=true) const {
        return element<Text>(page, text, size, 0, 1, contentSize.x, font, false, interlineStretch, center);
    }

    // Parser

    /// Skips whitespaces and comments
    void skip(TextData& s) const {
        for(;;) {
            s.whileAny(" \n"_);
            if(s.match('%')) s.line(); // Comment
            else break;
        }
    }

    /// Parses a subscript expression
    String parseSubscript(TextData& s, const ref<string>& delimiters) const {
        ref<string> lefts {"["_,"{"_,"⌊"_};
        ref<string> rights{"]"_,"}"_,"⌋"_};

        String subscript;
        if(!s.wouldMatchAny(lefts)) subscript << s.next();
        for(;;) {
            assert_(s, subscript);
            if(s.wouldMatchAny(delimiters)) break;
            else if(s.match('_')) subscript << parseSubscript(s, delimiters);
            else {
                for(int index: range(lefts.size)) {
                    if(s.match(lefts[index])) {
                        if(index>=2) subscript << lefts[index];
                        String content = parseLine(s, {rights[index]}, true);
                        assert_(content);
                        subscript << content;
                        if(index>=2) subscript << rights[index];
                        goto break_;
                    }
                } /*else*/
                if(s.wouldMatchAny(" \t\n,;()^/+-|"_) || s.wouldMatchAny({"·"_,"⌋"_,"²"_})) break;
                else subscript << s.next();
                break_:;
            }
        }
        assert_(s && subscript.size && subscript.size<=15, "Expected subscript end delimiter  _]), got end of document",
                "'"_+subscript.slice(0, min(subscript.size, 16ul))+"'"_, subscript.size, delimiters, s.buffer);
        String text;
        text << (char)(TextFormat::SubscriptStart);
        text << subscript;
        text << (char)(TextFormat::SubscriptEnd);
        return text;
    }

    /// Parses a line expression
    String parseLine(TextData& s, const ref<string>& delimiters={"\n"_}, bool match=true) const {
        String text; bool bold=false,italic=false;
        for(;;) { // Line
            assert_(s);
            if(match ? s.matchAny(delimiters) : s.wouldMatchAny(delimiters)) break;

            /**/ if(s.match('*')) { text << (char)(TextFormat::Bold); bold=!bold; }
            else if(s.match("//"_)) text << "/"_;
            else if(s.match('/')) { text << (char)(TextFormat::Italic); italic=!italic; }
            else if(s.match('\\')) text << s.next();
            else if(s.match('_')) text << parseSubscript(s, delimiters);
            else if(s.match('^')) {
                text << (char)(TextFormat::Superscript);
                String superscript = parseLine(s,{" "_,"\n"_,"("_,")"_,"^"_,"/"_,"|"_,"·"_,"⌋"_}, false);
                if(superscript.size<1 || superscript.size>14) {
                    log("Expected superscript end delimiter  ^]), got end of document", superscript.size, "'"_+superscript+"'"_);
                    log("'"_+(string)text.slice(0, min(text.size,14ul))+"'"_);
                    log(s.buffer);
                    error(""_);
                }
                text << superscript;
                text << (char)(TextFormat::Superscript);
            }
            else text << s.next();
        }
        assert_(!bold, "Expected bold end delimiter *, got end of line");
        assert_(!italic, "Expected italic end delimiter /, got end of line");
        return text;
    }

    /// Parses a layout expression
    Widget* parseLayout(TextData& s, Page& page, bool quick) const {
        array<Widget*> children;
        char type = 0;
        while(!s.match(')')) {
            // Element
            skip(s);
            if(s.match('(')) children << parseLayout(s, page, quick);
            else if(s.wouldMatchAny("&_^@"_)) {
                string prefix = s.whileAny("&_^@"_);
                string path = s.untilAny(" \t\n"_);
                if(quick) {
                    //TODO: Layout dummy box with image size for page numbers in table of contents with auto page break
                } else {
                    unique<Image> image;
                    /**/ if(existsFile(path)) image = unique<Image>(decodeImage(readFile(path)));
                    else if(existsFile(path+".png"_)) image = unique<Image>(decodeImage(readFile(path+".png"_)));
                    else if(existsFile(path+".jpg"_)) image = unique<Image>(decodeImage(readFile(path+".jpg"_)));
                    else error("Missing image", path);
                    for(char operation: prefix.reverse()) {
                        if(operation=='@') image = unique<Image>(rotate(image));
                        else if(operation=='^') image = unique<Image>(upsample(image));
                        else if(operation=='_') image = unique<Image>(downsample(image));
                        else if(operation=='&') {}
                        else error("Unknown image operation", operation);
                    }
                    while(!(image->size() <= contentSize)) image = unique<Image>(downsample(image));
                    children << &element<ImageWidget>(page, image);
                    page.images << move(image);
                }
            } else {
                String text = simplify(parseLine(s, {"\n"_,"_"_,"-"_,"+"_,")"_}, false));
                assert_(text, s.line());
                children << &newText(page, text, textSize);
                if(s.match('\n')) continue;
            }
            skip(s);
            // Separator
            /**/ if(s.match(')')) break;
            else if(!type) type = s.next();
            else s.skip(string(&type,1));
        }
        if(type=='-') return &element<VBox>(page, move(children));
        else if(type=='|') return &element<HBox>(page, move(children));
        else if(type=='+') return &element<WidgetGrid>(page, move(children));
        else { assert_(children.size==1); return children.first(); }
    }

    /// Parses a page statement
    /// \arg quick Quick layout for table of contents (skips images)
    Page parsePage(TextData& s, array<uint>& indices, uint pageIndex, bool quick=false) const {
        Page page (pageIndex, marginPx);
        while(s) {
            while(s.match('%')) s.line(); // Comment

            if(s.match('(')) { // Float
                page << parseLayout(s, page, quick);
                s.skip("\n"_);
                continue;
            }

            if(s.match('-')) { // List
                VBox& list = element<VBox>(page, VBox::Even);
                do {
                    list << &newText(page, "- "_+parseLine(s), textSize, false);
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
                    auto& vbox = element<VBox>(page, Linear::Top, Linear::Expand);
                    for(const Header& header: headers) {
                        String text;
                        text << repeat(" "_, header.indices.size);
                        if(header.indices.size<=1) text << (char)(TextFormat::Bold);
                        for(int level: header.indices) text << dec(level) << '.';
                        text << ' ' << header.name;
                        auto& hbox = element<HBox>(page, Linear::Spread);
                        hbox << &newText(page, text, textSize);
                        hbox << &newText(page, " "_+dec(header.page), textSize);
                        vbox << &hbox;
                    }
                    page << &vbox;
                } else error(command);
                continue;
            }

            String text = center ? String(""_) : repeat(" "_, 4);

            if(level) {
                if(level > indices.size) indices.grow(level);
                if(level < indices.size) indices.shrink(level);
                indices[level-1]++;
                /*if(level==1)*/ bold = true;
            }

            if(bold) text << (char)(TextFormat::Bold);
            if(level) {
                for(int level: indices) text << dec(level) << '.';
                text << ' ';
            }

            String userText;
            while(s && !s.match('\n')) userText << parseLine(s) << ' ';
            if(userText) {
                text << trim(userText);
                if(level && pageIndex==pages.size) page.headers << Header{copy(indices), move(userText), pageIndex};
                page << &newText(page, text, size, center);
            }

            if(s.match('\n')) break; // Page break
        }
        if(page.index) page.footer = &newText(page, dec(page.index), textSize);
        return move(page);
    }
    Page parsePage(TextData&& s, array<uint>&& indices, uint pageIndex) const { return parsePage(s, indices, pageIndex); }
    Page parsePage(uint pageIndex) const {
        assert_(pageIndex<pages.size, pageIndex, pages.size);
        return parsePage(TextData(pages[pageIndex]), copy(indices[pageIndex]), pageIndex);
    }

    /*buffer<byte> toPDF() {
        uint pageIndex = 0;
        for(TextData s (source); s;) {
            parsePage(pageIndex);
            pageIndex++;
        }
        pageCount = pageIndex;
    }*/
};

struct PageView : Widget {
    int pageCount;
    function<Page(int)> getPage;
    int pageIndex;
    Page page = getPage(pageIndex);

    PageView(int pageCount, function<Page(int)> getPage, int pageIndex=0)
        : pageCount(pageCount), getPage(getPage), pageIndex(pageIndex) {}

    bool keyPress(Key key, Modifiers) override {
        /**/ if(key == LeftArrow) pageIndex = max(0, pageIndex-1);
        else if(key == RightArrow) pageIndex = min(pageCount-1, pageIndex+1);
        else return false;
        page = getPage(pageIndex);
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) override {
        setFocus(this);
        /**/ if(button==WheelUp) pageIndex = max(0, pageIndex-1);
        else if(button==WheelDown) pageIndex = min(pageCount-1, pageIndex+1);
        else return false;
        page = getPage(pageIndex);
        return true;
    }

    void render() override { page.Widget::render(target); }
};

struct DocumentViewer {
    const string path;
    const String lastPageIndexPath = "."_+path+".last-page-index"_;
    const int lastPageIndex = existsFile(lastPageIndexPath) ? fromInteger(readFile(lastPageIndexPath)) : 0;
    Document document { readFile(path) };
    PageView view {(int)document.pages.size, {this, &DocumentViewer::getPage}, lastPageIndex};
    Window window {&view, document.pageSize, dec(view.pageIndex)};

    Page getPage(int pageIndex) {
        if(window) {
            window.render();
            window.setTitle(dec(pageIndex));
            writeFile(lastPageIndexPath, dec(pageIndex));
        }
        return document.parsePage(pageIndex);
    }

    FileWatcher watcher{path, [this](string){ document.~Document(); new (&document) Document(readFile(path)); } };

    DocumentViewer(const string path) : path(path) {}
};

struct DocumentApp {
    unique<DocumentViewer> viewer = nullptr;
    DocumentApp() {
        assert_(arguments().size==2 && ref<string>({"preview"_,"export"_}).contains(arguments()[1]), "Usage: <path> preview|export");
        /***/ if(arguments()[1]=="preview"_) viewer = unique<DocumentViewer>(arguments()[0]);
        /*else if(arguments()[1]=="export"_) {
            writeFile("rapport.pdf"_, imagesToPDF(document.pages()));
                exit(0);
                return;
            }
            else
        }*/
        else error(""_);
    }
} app;
