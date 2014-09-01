#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
//#include "png.h"
//#include "jpeg.h"
#include "pdf.h"

uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }

String escape(string s) { return replace(replace(s,"\t"_,"\\t"_),"\n"_,"\\n"_); }

struct Placeholder : Widget {
    Graphics graphics(int2) const override { return {}; }
};

/// Header of a section from a \a Document
struct Header {
    array<uint> indices; // Header index for each level
    String name; // Header name
    uint page; // Header page index
};
Header copy(const Header& o) { return {copy(o.indices), copy(o.name), o.page}; }

/// \a Page from a \a Document
struct Page : VBox {
    uint index;
    int2 marginPx;
    array<Header> headers;
    array<unique<Widget>> elements;
    array<unique<Image>> images;
    Widget* footer = 0;

    Page(Linear::Extra main, uint index, int2 marginPx)
        : Linear(main, Linear::Expand, true), index(index), marginPx(marginPx) {}

    Graphics graphics(int2 size) const override {
        Graphics graphics;
        int2 inner = size - 2*marginPx;
        graphics.append(VBox::graphics(inner), vec2(marginPx));
        if(this->footer) graphics.append( this->footer->graphics(int2(size.x, marginPx.y)), vec2(0, size.y - marginPx.y));
        return graphics;
    }
};

struct Format {
    const int2 pageSize;
    int2 marginPx;
    const string font;
    const float footerSize, textSize, headerSize, titleSize;
    float pointPx; // Pixel per point (1/72 dpi)
};
struct A4 : Format {
    static constexpr float inchMM = 25.4, inchPx = 90;
    static constexpr int pageWidth = 210/*mm*/ * (inchPx/inchMM), pageHeight = 297/*mm*/ * (inchPx/inchMM);
    static constexpr float pointPx = inchPx / 72;
    A4() : Format{int2(pageWidth, pageHeight), int2(1.5 * inchPx), "FreeSerif"_, /*0*/12 * pointPx, 12 * pointPx, 14 * pointPx, 16 * pointPx, pointPx} {}
};

/// Layouts a document
struct Document {
    const String source;
    string formatString;
    Format format = formatString == "A4"_ ? A4() : Format{int2(1024,768), int2(64,0), "DejaVuSans"_, 0, 24, 24, 32, 1};
    const float interlineStretch = 3./2;

    // Document properties
    array<Header> headers;
    array<string> pages; // Source slices for each page
    array<Header> firstHeaders; // first header of each page

    /// Generates page starts table, table of contents
    Document(String&& source_) : source(move(source_)), formatString(startsWith(source,"%"_)?section(source.slice(1),'\n'):"A4"_) {
        assert_(format.pageSize - 2*format.marginPx > int2(0), format.pageSize, format.marginPx);
        assert_(!source.contains('\r'));
        array<uint> indices;
        Header current;
        for(TextData s (source); s;) {
            uint start = s.index;
            firstHeaders << copy(current);
            Page page = parsePage(s, current, pages.size, true);
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
    Text& newText(Page& page, string text, float size, bool center=true) const {
        return element<Text>(page, text, size, 0, 1, (format.pageSize - 2*format.marginPx).x, format.font, false, interlineStretch, center);
    }

    // Parser

    template<Type... Args> String warn(TextData& s, const Args&... args) const {
        String text = str(s.lineIndex)+": "_+str(args...); log(text); return text;
    }
    template<Type... Args> Text& warnText(TextData& s, Page& page, const Args&... args) const {
        return newText(page, warn(s, args...), format.textSize);
    }

    /// Parses a sub|super script expression
    String parseScript(TextData& s, const ref<string>& delimiters) const {
        ref<string> lefts {"["_,"{"_,"⌊"_};
        ref<string> rights{"]"_,"}"_,"⌋"_};

        String e;
        if(!s.wouldMatchAny(lefts)) e << s.next();
        for(;;) {
            if(!s || s.wouldMatchAny(delimiters)) break;
            else if(s.match('_')) e << subscript(parseScript(s, delimiters));
            else {
                for(int index: range(lefts.size)) {
                    if(s.match(lefts[index])) {
                        if(index>=2) e << lefts[index];
                        String content = parseText(s, {rights[index]});
                        s.skip({rights[index]});
                        assert_(content);
                        e << content;
                        if(index>=2) e << rights[index];
                        goto break_;
                    }
                } /*else*/
                if(s.wouldMatchAny(" \t\n,.;()^/+-|*"_) || s.wouldMatchAny({"\xC2\xA0"_,"·"_,"⌋"_,"²"_})) break;
                else e << s.next();
                break_:;
            }
        }
        if(e.size==0 || e.size>15) warn(s,  "Long subscript"_);
        return e;
    }

    /// Parses a text expression
    String parseText(TextData& s, const ref<string>& delimiters={"\n"_}) const {
        s.whileAny(' ');
        String text; bool bold=false,italic=false;
        while(s) { // Line
            if(s.wouldMatchAny(delimiters)) break;

            /**/ if(s.match('%')) { s.whileNo("%\n"_,'(',')'); s.match('%'); } // Comment
            else if(s.match('*')) { text.append( bold?TextFormat::End:TextFormat::Bold ); bold=!bold; }
            else if(s.match("//"_)) text << "/"_;
            else if(s.match('/')) { text.append( italic?TextFormat::End:TextFormat::Italic ); italic=!italic; }
            else if(s.match('\\')) {
                /***/ if(s.match('n')) text << '\n';
                else if(s.match('t')) text << '\t';
                else text << s.next();
            } else if(s.match('_')) text << subscript(parseScript(s, delimiters));
            else if(s.match('^')) text << superscript(parseScript(s, delimiters));
            else if(s.match('{')) {
                String num = regular(trim(parseText(s, {"/"_,"}"_,"\n"_})));
                if(!s.match('/')) warn(s, "Expected / stack delimiter, got '"_+string{s.peek()}+"'"_);
                bool fraction = s.match('/');
                String den = regular(trim(parseText(s, {"}"_,"\n"_})) ?: "?"_);
                if(!s.match('}')) warn(s, "Expected } stack delimiter, got '"_+string{s.peek()}+"'"_);
                text << (fraction ? ::fraction(num+den) : ::stack(num+den));
            }
            else text << s.next();
        }
        if(bold) warn(s, "Expected bold end delimiter *, got end of line");
        if(italic) warn(s, "Expected italic end delimiter /, got end of line");
        return text;
    }

    /// Parses a layout expression
    Widget* parseLayout(TextData& s, Page& page, bool quick) const {
        s.skip('('); s.whileAny(" "_);
        array<Widget*> children;
        char type = 0; int width=0;
        while(!s.match(')')) {
            children << parseWidget(s, page, quick);
            s.whileAny(" "_);
            // Separator
            if((type=='+' || type=='$') && s.match('\n')) { if(!width) width=children.size; if(children.size%width) warn(s, children.size ,width);/*FIXME*/ }
            else if(!type && s.match('\n')) {
                s.whileAny(" \n"_);
                if(s.wouldMatchAny("-|+$"_)) type = s.next();
                else type='\n';
            }
            else if(type=='\n' && s.match('\n')) {}
            else if((!type || type=='@') && s.wouldMatch('@')) type='@';
            else {
                s.whileAny(" \n"_); // \n might be tight list or array width specifier
                /**/ if(s.match(')')) break;
                else if(type && s.match(type)) {}
                else if(!type && s.wouldMatchAny("-|+$"_)) type = s.next();
                else {
                    children << &warnText(s, page, "Expected "_+(type?"'"_+str(type)+"'"_:"-, |, +, $ "_), "or ), got '"_+str(s?string{s.peek()}:"EOD"_)+"'"_);
                    break;
                }
            }
        }
        if(type=='-') return &element<VBox>(page, move(children), VBox::Spread, VBox::AlignCenter, false);
        else if(type=='|' || type=='@') return &element<HBox>(page, move(children), HBox::Share, HBox::AlignCenter, true);
        else if(type=='+') return &element<WidgetGrid>(page, move(children), false, false, width); //TODO: Expanding
        else if(type=='$') return &element<WidgetGrid>(page, move(children), true, true, width);
        else if(type=='\n') return &element<VBox>(page, move(children), VBox::Center, VBox::AlignCenter, false);
        else if(!type) {
            if(!children) return &warnText(s, page, "Empty layout");
            assert_(children.size==1);
            return children.first();
        }
        else error("Unknown layout type", type);
    }

    Widget* parseImage(TextData& s, Page& page, bool quick) const {
        s.skip('@');
        bool negate = s.match('-');
        string path = s.whileNo(" \t\n)-|+$"_);
        if(quick) return &element<Placeholder>(page); //FIXME
        Image image;
        /**/ if(existsFile(path)) image = decodeImage(readFile(path));
        else if(existsFile(path+".png"_)) image = decodeImage(readFile(path+".png"_));
        else if(existsFile(path+".jpg"_)) image = decodeImage(readFile(path+".jpg"_));
        if(!image) return &warnText(s, page, "Missing image", path);
        if(negate) image = ::negate(move(image), image);
        page.images << unique<Image>(move(image));
        return &element<ImageWidget>(page, page.images.last());
    }

    Widget* parseList(TextData& s, Page& page) const {
        s.skip('-');
        VBox& list = element<VBox>(page, VBox::Even, VBox::AlignLeft, true);
        do {
            list << &newText(page, "- "_+parseText(s), format.textSize, false);
            if(s) s.skip("\n"_);
        } while(s.match('-'));
        return &list;
    }

    /// Parses a widget expression
    Widget* parseWidget(TextData& s, Page& page, bool quick) const {
        s.whileAny(' ');
        if(s.wouldMatch('(')) return parseLayout(s, page, quick);
        else if(s.wouldMatch('@')) return parseImage(s, page, quick);
        else if(s.wouldMatch('-')) return parseList(s, page);
        else {
            String text;
            if(s.match('"')) { text = parseText(s, {"\""_}); s.skip("\""_); }
            else text = parseText(s, {"\n"_,"|"_,"-"_,"+"_,"$"_,")"_});
            return &newText(page, trim(text), format.textSize);
        }
    }

    /// Parses a page
    /// \arg quick Quick layout for table of contents (skips images)
    Page parsePage(TextData& s, Header& currentHeader, uint pageIndex, bool quick=false) const {
        Page page (Linear::ShareTight, pageIndex, format.marginPx);
        while(s) {
            // Header
            /***/ if(s.wouldMatch('#')) {
                uint level = 0;
                while(s.match('#')) level++;
                s.whileAny(' ');
                if(s && !s.match('\n')) { // New header
                    if(level > currentHeader.indices.size) currentHeader.indices.grow(level);
                    if(level < currentHeader.indices.size) currentHeader.indices.shrink(level);
                    currentHeader.indices[level-1]++;
                    currentHeader.name = String(parseText(s));
                    currentHeader.page = pageIndex;
                    if(pageIndex==pages.size) // Table of contents pass
                        page.headers << copy(currentHeader);
                }
                String text = String{TextFormat::Bold};
                for(int level: currentHeader.indices) text << dec(level) << '.';
                assert_(currentHeader.name, apply(firstHeaders,[](const Header& e)->string{ return e.name; }));
                text << ' ' << currentHeader.name;
                page << &newText(page, bold(text), format.headerSize, false);
            }
            // Widget
            else if(s.wouldMatchAny("(@-\""_)) {
                page << parseWidget(s, page, quick);
            }
            // Command
            else if(s.match('\\')) {
                string command = s.whileNot('\n');
                if(command == "tableofcontents"_) {
                    auto& grid = element<WidgetGrid>(page, false, false, format.footerSize ? 2 /*Show page numbers*/: 1);
                    for(const Header& header: headers) {
                        String text;
                        text << repeat(" "_, header.indices.size);
                        if(header.indices.size<=1) text << (char)(TextFormat::Bold);
                        for(int level: header.indices) text << dec(level) << '.';
                        text << ' ' << TextData(header.name).until('(');
                        grid << &newText(page, text, format.textSize, false);
                        if(format.footerSize) grid << &newText(page, " "_+dec(header.page), format.textSize); // Show page numbers
                    }
                    page << &grid;
                } else error(command);
                continue;
            }
            // Centered text
            else if(s.match(' ')) {
                if(s.match('#')) warn(s, "' #' will render centered text instead of header");
                page << &newText(page, parseText(s), format.textSize, true);
            }
            // Large centered text (title)
            else if(s.match('!')) page << &newText(page, bold(parseText(s)), format.titleSize, true);
            // Page break (double blank line)
            else if(s.match("\n\n\n"_)) break;
            // Comment
            else if(s.match('%')) { s.whileNo("%\n"_,'(',')'); s.match('%'); }
            // Line feed
            else if(s.match('\n')) {}
            // Paragraph
            else {
                assert_(s && !s.wouldMatchAny("\n ("_));
                String text = parseText(s);
                while(s && !s.wouldMatchAny("\n ("_)/*single blank line or center statement*/) text << ' ' << parseText(s);
                assert_(text, "Unexpected '"_+escape(text)+"' at line"_, s.lineIndex);
                page << &newText(page, "\t"_+text, format.textSize, false);
            }
        }
        if(format.footerSize && page.index>=3) page.footer = &newText(page, dec(page.index), format.textSize);
        return move(page);
    }
    Page parsePage(TextData&& s, Header&& firstHeader, uint pageIndex) const { return parsePage(s, firstHeader, pageIndex); }
    Page parsePage(uint pageIndex) const {
        assert_(pageIndex<pages.size, pageIndex, pages.size);
        return parsePage(TextData(pages[pageIndex]), copy(firstHeaders[pageIndex]), pageIndex);
    }

    buffer<byte> toPDF() {
        // Releases pages after toPDF to keep valid weak references to images in graphics
        return ::toPDF(format.pageSize, apply( apply(pages.size, [&](int index){ return parsePage(index);}),
                                               [&](const Page& page) { return page.graphics(format.pageSize); }), 1./format.pointPx);
    }
};

struct PageView : Widget {
    int pageCount;
    function<Page(int)> getPage;
    int pageIndex;
    Page page = getPage(pageIndex);

    PageView(int pageCount, function<Page(int)> getPage, int pageIndex=0)
        : pageCount(pageCount), getPage(getPage), pageIndex(min(pageIndex, pageCount-1)) {}

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

    Graphics graphics(int2 size) const override { return page.graphics(size); }
};

struct DocumentViewer {
    const string path;
    const String lastPageIndexPath = "."_+path+".last-page-index"_;
    Document document { readFile(path) };
    const int lastPageIndex = existsFile(lastPageIndexPath) ? fromInteger(readFile(lastPageIndexPath)) : 0;
    PageView view {(int)document.pages.size, {this, &DocumentViewer::getPage}, lastPageIndex};
    Window window {&view, document.format.pageSize, dec(view.pageIndex)};

    Page getPage(int pageIndex) {
        if(window) {
            window.render();
            window.setTitle(dec(pageIndex));
            writeFile(lastPageIndexPath, dec(pageIndex));
        }
        return document.parsePage(pageIndex);
    }

    FileWatcher watcher{path, [this](string){ //TODO: watch images
            document.~Document(); new (&document) Document(readFile(path));
            view.pageCount = document.pages.size;
            view.pageIndex = min(view.pageIndex, view.pageCount-1);
            view.page = document.parsePage(view.pageIndex);
            window.render();
            window.show();
        } };

    DocumentViewer(const string path) : path(path) {}
};

struct DocumentApp {
    unique<DocumentViewer> viewer = nullptr;
    DocumentApp() {
        assert_(arguments().size==2 && ref<string>({"preview"_,"export"_}).contains(arguments()[1]), "Usage: <path> preview|export");
        string path = arguments()[0], command = arguments()[1];
        /***/ if(command=="preview"_) viewer = unique<DocumentViewer>(path);
        else if(command=="export"_) writeFile(section(path,'.',0,-2)+".pdf"_, Document(readFile(path)).toPDF());
        else error("Unknown command"_, arguments());
    }
} app;
