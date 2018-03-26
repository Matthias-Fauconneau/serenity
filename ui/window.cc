#include "window.h"
#include "x.h"
#include "time.h"
#include "image-render.h"
#include <sys/shm.h> // X11

#if GL
#include "gl.h"
// GLX/Xlib/DRI2
#define Time XTime
#define Cursor XCursor
#define Window XXWindow
#define XEvent XXEvent
#define Display XXDisplay
#include <GL/glew.h> // GLEW
#include <GL/glx.h> // GL
#undef Time
#undef Cursor
#undef Window
#undef XEvent
#undef Display

struct RenderTargetGL : RenderTarget2D {
    RenderTargetGL(vec2 size) : RenderTarget2D(size) {}
    virtual ~RenderTargetGL();
    virtual void fill(vec2, vec2, bgr3f, float) override { error("UNIMPL fill"); }
    virtual void blit(vec2, vec2, Image&&, bgr3f, float) override { error("UNIMPL blit"); }
    virtual void glyph(vec2, float, FontData&, uint, uint, bgr3f, float) override { error("UNIMPL glyph"); }
    virtual void line(vec2, vec2, bgr3f, float, bool) override { error("UNIMPL line"); }
    virtual void trapezoidY(Span, Span, bgr3f, float) override { error("UNIMPL trapezoidY"); }
};
RenderTargetGL::~RenderTargetGL() {}
#endif

Image cursorIcon();
using namespace X11;

static Window* currentWindow = 0; // FIXME
bool hasFocus(Widget* widget) { assert_(currentWindow); return currentWindow->focus==widget; }
void setCursor(MouseCursor cursor) { assert_(currentWindow); currentWindow->setCursor(cursor); }
String getSelection(bool clipboard) { assert(currentWindow); return currentWindow->getSelection(clipboard); }
void setSelection(string selection, bool clipboard) { assert(currentWindow); return currentWindow->setSelection(selection, clipboard); }

void Window::render() { update=true; queue(); }

void Window::render(const Image& target) {
    assert_(update);
    currentWindow = this; // hasFocus
    ImageRenderTarget renderTarget(unsafeShare(target));
    update = false; // Lets Window::render within Widget::render trigger new rendering on next event
    widget->render(renderTarget);
    currentWindow = 0;
}

#if GL
static void glDebugMessage(uint source, uint type, uint id, uint severity, int length, const char* message, const void*) {
    String s = str(  ref<string>{"API","WindowSystem","ShaderCompiler","3rdParty","Application","Other"}[source-0x8246],
                     ref<string>{"Error","Deprecated","Undefined","Portability","Performance","Other"}[type-0x824C],
                     severity==GL_DEBUG_SEVERITY_NOTIFICATION?"Notification":ref<string>{"High","Medium","Low"}[severity-0x9146],
                     hex(id), ref<char>(message, size_t(length)));
    log(s);
    if(source==GL_DEBUG_SOURCE_SHADER_COMPILER&&type==GL_DEBUG_TYPE_OTHER&&(severity==GL_DEBUG_SEVERITY_NOTIFICATION||id<=4)) {} else error(s);
}
#endif

XWindow::XWindow(Widget* widget, Thread& thread, int2 sizeHint, int useGL_samples) :
    ::Window(widget, thread, sizeHint), XDisplay(thread) {
    XDisplay::onEvent.connect(this, &XWindow::onEvent);
    assert_(id && root && visual);

    if(sizeHint.x<=0) Window::size.x=XDisplay::size.x;
    if(sizeHint.y<=0) Window::size.y=XDisplay::size.y;
    if((sizeHint.x<0||sizeHint.y<0) && widget) {
        int2 hint (widget->sizeHint(vec2(Window::size)));
        if(sizeHint.x<0) Window::size.x=min(max(uint(abs(hint.x)),uint(-sizeHint.x)), XDisplay::size.x);
        if(sizeHint.y<0) Window::size.y=min(max(uint(abs(hint.y)),uint(-sizeHint.y)), XDisplay::size.y-46);
    }
    assert_(Window::size);
    {CreateColormap r; send(({r.colormap=id+Colormap; r.window=root; r.visual=visual; r;}));}
    {CreateWindow r;
        send(({r.id=id+Window; r.parent=root; r.width=uint16(Window::size.x); r.height=uint16(Window::size.y); r.visual=visual; r.colormap=id+Colormap; r;}));}
    assert_(Present::EXT);
    if(Present::EXT) send(({Present::SelectInput r; r.window=id+Window; r.eid=id+PresentEvent; r;}));
    Window::actions[Escape] = []{requestTermination();};
    {CreateGC r; send(({r.context=id+GraphicContext; r.window=id+Window; r;}));}

    event();

    if(useGL_samples) {
#if GL
        assert_(useGL_samples==1);
        const int fbAttribs[] = {//GLX_DOUBLEBUFFER, 1,
                         #if NVIDIA
                                 GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, 1,
                         #endif
                                 //GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, //GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24,
                                 //GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, useGL_samples,
                                 //GLX_COLOR_SAMPLES_NV, 1, GLX_COVERAGE_SAMPLES_NV, useGL_samples,
                                 0};
        int fbCount=0;
        GLXFBConfig* fbConfigs = glXChooseFBConfig(glDisplay, 0, fbAttribs, &fbCount);
        for(GLXFBConfig fbConfig: ref<GLXFBConfig>(fbConfigs, (size_t)fbCount)) {
            int value;
            assert_( glXGetFBConfigAttrib(glDisplay, fbConfig, GLX_VISUAL_ID, &value) == 0);
            if(uint(value) == visual) {
                this->fbConfig = fbConfig;
                break;
            }
        }
        assert_(fbConfig);
        XFree(fbConfigs);
        this->glContext = initializeThreadGLContext();
        glewInit();
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugMessage, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, true);
        // Framebuffer detailed info: The driver allocated storage for renderbuffer.
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, (uint[]){0x20061}, false);
        // Buffer detailed info: Buffer object (bound to GL_ARRAY_BUFFER_ARB, usage hint is GL_STATIC_DRAW) will use VIDEO memory as the source.
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, (uint[]){0x20071}, false);
        // Buffer usage warning: Analysis of buffer object (bound to GL_SHADER_STORAGE_BUFFER) usage indicates that
        // the GPU is the primary producer and consumer of data for this buffer object.
        // The usage hint supplied with this buffer object, GL_STATIC_DRAW, is inconsistent with this usage pattern.
        // Try using GL_STREAM_COPY_ARB, GL_STATIC_COPY_ARB, or GL_DYNAMIC_COPY_ARB instead.
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, (uint[]){0x20074}, false);
        // Program/shader state info: GLSL shader failed to compile.
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, (uint[]){0x20090}, false);
#else
        error("UNIMPL");
#endif
    }

    /*{ChangeProperty r; r.window=id+Window; r.property=Atom("_NET_WM_STATE"_); r.type=Atom("ATOM"_); r.format=32; r.length=5; r.size+=r.length;
        send(r, raw((uint[5]){1, Atom("_NET_WM_STATE_FULLSCREEN"),0,0,0})); }*/
}

XWindow::~XWindow() {
#if GL
    if(glContext) glXDestroyContext(glDisplay, glContext);
    if(glDisplay) XCloseDisplay(glDisplay);
#endif
    if(id) {
        {DestroyWindow r; send(({r.id=id+Window; r;}));}
        id = 0;
    }
}

// Events
void XWindow::onEvent(const ref<byte> ge) {
    const X11::Event& event = *reinterpret_cast<const X11::Event*>(ge.data);
    uint8 type = event.type&0b01111111; //msb set if sent by SendEvent
    if(type==MotionNotify) { heldEvent = unique<X11::Event>(event); Window::queue(); }
    else {
        if(heldEvent) { assert_(processEvent(heldEvent)); heldEvent=nullptr; }
        if(processEvent(event)) {}
        else if(type==GenericEvent && event.genericEvent.ext == Present::EXT && event.genericEvent.type==Present::CompleteNotify) {
            const auto& completeNotify = *reinterpret_cast<const struct Present::CompleteNotify*>(&event);
            assert_(sizeof(X11::Event)+event.genericEvent.size*4 == sizeof(completeNotify),
                    sizeof(X11::Event)+event.genericEvent.size*4, sizeof(completeNotify));
            state = Idle;
            currentFrameCounterValue = completeNotify.msc;
            if(!firstFrameCounterValue) firstFrameCounterValue = currentFrameCounterValue;
            if(presentComplete) presentComplete();
        }
        else error("Unhandled event"_, ref<string>(X11::events)[type]);
    }
}

bool XWindow::processEvent(const X11::Event& e) {
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
    currentWindow = this; // FIXME
    /**/ if(type==ButtonPress) {
        Widget* previousFocus = focus;
        if((widget && widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Press, Button(e.key), focus)) || focus!=previousFocus) render();
        drag = focus;
    }
    else if(type==ButtonRelease) {
        drag=0;
        if(widget && e.key <= RightButton && widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Release, Button(e.key), focus)) render();
    }
    else if(type==KeyPress) {
        Key key = Key(keySym(e.key, uint8(e.state))); Modifiers modifiers = (Modifiers)e.state;
        if(focus && focus->keyPress(key, modifiers)) render(); // Normal keyPress event
        else {
            function<void()>* action = Window::actions.find(key);
            if(action) (*action)(); // Local window action
        }
    }
    else if(type==KeyRelease) {
        Key key = (Key)keySym(e.key, uint8(e.state)); Modifiers modifiers = (Modifiers)e.state;
        if(focus && focus->keyRelease(key, modifiers)) render();
    }
    else if(type==MotionNotify) {
        if(widget && widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Motion,
                                        (e.state&Button1Mask)?LeftButton:(e.state&Button3Mask)?RightButton:NoButton, focus))
            render();
    }
    else if(type==EnterNotify || type==LeaveNotify) {
        if(widget && widget->mouseEvent( vec2(e.x,e.y), vec2(Window::size), type==EnterNotify?Enter:Leave,
                               e.state&Button1Mask?LeftButton:NoButton, focus) ) render();
    }
    else if(type==KeymapNotify) {}
    else if(type==Expose) { /*if(!e.expose.count && !(e.expose.x==0 && e.expose.y==0 && e.expose.w==1 && e.expose.h==1)) render();*/ }
    else if(type==UnmapNotify) mapped=false;
    else if(type==MapNotify) { mapped=true; render(); }
    else if(type==ReparentNotify) {}
    else if(type==ConfigureNotify) { Window::size = uint2(uint(e.configure.w), uint(e.configure.h)); }
    else if(type==GravityNotify) {}
    else if(type==ClientMessage) {
        function<void()>* action = Window::actions.find(Escape);
        if(action) (*action)(); // Local window action
        else if(focus && focus->keyPress(Escape, NoModifiers)) render(); // Translates to Escape keyPress event
        else requestTermination(0); // Exits application by default
    }
    else if(type==MappingNotify) {}
    else if(type==Shm::event+Shm::Completion) {
        assert_(state == Copy, int(state));
        if(Present::EXT) state = Present; else { state = Idle; if(presentComplete) presentComplete(); }
    }
    else { currentWindow = 0; return false; }
    currentWindow = 0;
    return true;
}

void XWindow::show() { {MapWindow r; send(({ r.id=id; r;}));} {RaiseWindow r; send(({r.id=id; r;}));} }
void XWindow::hide() { {UnmapWindow r; send(({r.id=id; r;}));} }

void XWindow::setTitle(string title) {
    if(!title || title == this->title) return;
    this->title = copyRef(title);
    {ChangeProperty r; send(({r.window=id+Window; r.property=Atom("_NET_WM_NAME"); r.type=Atom("UTF8_STRING"); r.format=8;
                              r.length=uint(title.size); r.size=uint16(6+align(4, title.size)/4); r;}), title);}
}
void XWindow::setIcon(const Image&) {}
void XWindow::setSize(int2 sizeHint) {
    if(sizeHint.x<=0) Window::size.x=XDisplay::size.x;
    if(sizeHint.y<=0) Window::size.y=XDisplay::size.y;
    if((sizeHint.x<0||sizeHint.y<0) && widget) {
        int2 hint (widget->sizeHint(vec2(Window::size)));
        if(sizeHint.x<0) Window::size.x=min(max(uint(abs(hint.x)),uint(-sizeHint.x)), XDisplay::size.x);
        if(sizeHint.y<0) Window::size.y=min(max(uint(abs(hint.y)),uint(-sizeHint.y)), XDisplay::size.y-46);
    }
    assert_(Window::size);
    {SetSize r; send(({r.id=id+Window; r.w=uint(Window::size.x); r.h=uint(Window::size.y); r;}));}
}

void XWindow::event() {
    XDisplay::event();
    if(heldEvent) { assert_(processEvent(heldEvent)); heldEvent = nullptr; }
    if(getTitle || widget) setTitle(getTitle ? getTitle() : widget->title());
    if(state!=Idle || !mapped) return;

    if(target.size != Window::size) {
        if(target) {
            {FreePixmap r; send(({r.pixmap=id+Pixmap; r;}));} target=Image();
            assert_(shm);
            {Shm::Detach r; send(({r.seg=id+Segment; r;}));}
            shmdt(target.data);
            shmctl(shm, IPC_RMID, nullptr);
            shm = 0;
        } else assert_(!shm);

        const uint stride = (uint)align(16, Window::size.x);
        shm = check( shmget(0, Window::size.y*stride*sizeof(byte4) , IPC_CREAT | 0777) );
        target = Image(buffer<byte4>(reinterpret_cast<byte4*>(check(shmat(shm, nullptr, 0))), Window::size.y*stride, 0),
                       uint2(Window::size), stride, true);
        //target.clear(byte4(0xFF));
        {Shm::Attach r; send(({r.seg=id+Segment; r.shm=shm; r;}));}
        {CreatePixmap r; send(({r.pixmap=id+Pixmap; r.window=id+Window; r.w=uint16(Window::size.x); r.h=uint16(Window::size.y); r;}));}
    }

    if(update) {
#if GL
        if(glContext) {
            assert_(update);
            currentWindow = this; // hasFocus
            update = false; // Lets Window::render within Widget::render trigger new rendering on next event
            RenderTargetGL renderTarget {vec2(Window::size)};
            ::window(Window::size).bind(ClearColor|ClearDepth, rgba4f(backgroundColor, 1));
            //GLFrameBuffer::bindWindow(0, Window::size);
            widget->render(renderTarget, 0_0, vec2(Window::size));
            currentWindow = nullptr;
            swapTime.start();
            glXSwapBuffers(glDisplay, id+Window);
            swapTime.stop();
            if(presentComplete) presentComplete();
        } else
#endif
        {
            //target.clear(byte4(0xFF));
            render(target);
            {Shm::PutImage r; send(({r.window=id+(Present::EXT?Pixmap:Window);
                                     r.context=id+GraphicContext; r.seg=id+Segment;
                                     r.totalW=uint16(target.stride); r.totalH=uint16(target.size.y);
                                     r.srcX=uint16(0); r.srcY=uint16(0);
                                     r.srcW=uint16(target.size.x); r.srcH=uint16(target.size.y);
                                     r.dstX=uint16(0); r.dstY=uint16(0); r;}));}
            state = Copy;
            if(Present::EXT) send(({Present::Pixmap r; r.window=id+Window; r.pixmap=id+Pixmap; r;}));
        }
    }
}

#if GL
GLXContext XWindow::initializeThreadGLContext() {
 static Lock staticLock; Locker lock(staticLock);
 const int contextAttribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 5, GLX_CONTEXT_FLAGS_ARB,
                                GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, 0};
 GLXContext glContext = PFNGLXCREATECONTEXTATTRIBSARBPROC(glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")))
         (glDisplay, fbConfig, this->glContext, 1, contextAttribs);
 assert_(glContext);
 glXMakeCurrent(glDisplay, id+Window, glContext);
 return glContext;
}
#endif

void XWindow::setCursor(MouseCursor) {}

function<void()>& XWindow::globalAction(Key key) { return XDisplay::globalAction(key); }

String XWindow::getSelection(bool /*clipboard*/) { return {}; }
void XWindow::setSelection(string selection, bool clipboard) { this->selection[clipboard] = copyRef(selection); }

unique<Window> window(Widget* widget, int2 size, Thread& thread, int useGL_samples, string title) {
    if(environmentVariable("DISPLAY")) {
        auto window = unique<XWindow>(widget, thread, size, useGL_samples);
        if(title) window->setTitle(title);
        //window->show();
        return move(window);
    }
    error("");
}

