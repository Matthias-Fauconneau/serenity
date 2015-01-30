#include "file.h"
#include "data.h"
#include "x.h"
#include <sys/socket.h>
#include <unistd.h>
#include <gbm.h> // gbm
#include <EGL/egl.h> // EGL
#include <EGL/eglext.h> // drm
#include <GL/gl.h> // GL
extern "C" int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);

// -- Log
void log(string message) { String buffer = message+'\n'; check(write(2, buffer.data, buffer.size)); }
template<> void __attribute((noreturn)) error(const string& message) { log(message);  __builtin_trap(); }

// -- Display
namespace DRI3 { int EXT; }
namespace Present { int EXT; }

int socketFD = socket(PF_LOCAL,SOCK_STREAM|SOCK_CLOEXEC,0);
uint16 sequence = -1;
// Server
/// Base resource id
uint id = 0;
// Display
/// Root window
uint root = 0;
/// Root visual
uint visual = 0;

generic T read() {
	T t;
	read(socketFD, (byte*)&t, sizeof(T));
	return t;
}

generic buffer<T> read(size_t size) {
	buffer<T> buffer(size);
	read(socketFD, buffer.begin(), buffer.size*sizeof(T));
	return buffer;
}
buffer<byte> read(size_t size) { return read<byte>(size); }

uint16 sendRaw(ref<byte> data, int fd=-1) {
	iovec iov {.iov_base = (byte*)data.data, .iov_len = data.size};
	union { cmsghdr cmsghdr; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
	msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control)};
	if(fd==-1) { msg.msg_control = NULL, msg.msg_controllen = 0; }
	else {
		cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof (int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*((int *)CMSG_DATA(cmsg)) = fd;
	}
	ssize_t size = sendmsg(socketFD, &msg, 0);
	assert_(size == ssize_t(data.size), size, data.size);
	sequence++;
	return sequence;
}

buffer<byte> readReply(uint16 sequence, uint elementSize, buffer<int>& fds) {
	for(;;) {
		XEvent e;
		iovec iov {.iov_base = &e, .iov_len = sizeof(e)};
		union { cmsghdr cmsghdr; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
		msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control)};
		ssize_t size = recvmsg(socketFD, &msg, 0);
		assert_(size==sizeof(e));
		if(e.type==Reply) {
			assert_(e.seq==sequence, e.seq, sequence);
			buffer<byte> reply {raw(e.reply).size+e.reply.size, 0};
			reply.append(raw(e.reply));
			if(e.reply.size) { assert_(elementSize); reply.size += read(socketFD, reply.begin()+reply.size, e.reply.size); }
			cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
			if(cmsg) {
				assert_(cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
				assert_(cmsg->cmsg_level == SOL_SOCKET);
				assert_(cmsg->cmsg_type == SCM_RIGHTS);
				assert_(e.reply.padOrFdCount == 1);
				fds = buffer<int>(1);
				fds[0] = *((int*)CMSG_DATA(cmsg));
			}
			return reply;
		}
		if(e.type==Error) error("Error");
		log("Event");
	}
}

buffer<byte> pad(ref<byte> data, uint width=4) { buffer<byte> buffer(align(width, data.size)); buffer.slice(0, data.size).copy(data); return buffer; }
template<Type Request> uint16 send(Request request, const ref<byte> data, int fd=-1) {
	assert_(sizeof(request)%4==0 && sizeof(request) + align(4, data.size) == request.size*4);
	return sendRaw(ref<byte>(data?raw(request)+pad(data):raw(request)), fd);
}
template<Type Request> uint16 send(Request request, int fd=-1) { return send(request, {}, fd); }

template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, buffer<int>& fds, const ref<byte> data={}, int fd=-1) {
	static_assert(sizeof(typename Request::Reply)==31,"");
	uint16 sequence = send(request, data, fd);
	buffer<byte> replyData = readReply(sequence, sizeof(T), fds);
	typename Request::Reply reply = *(typename Request::Reply*)replyData.data;
	assert_(replyData.size == sizeof(typename Request::Reply)+reply.size*sizeof(T));
	output = copyRef(cast<T>(replyData.slice(sizeof(reply), reply.size*sizeof(T))));
	return reply;
}
template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, const ref<byte> data={}, int fd=-1) {
	buffer<int> fds;
	typename Request::Reply reply = ::request(request, output, fds, data, fd);
	assert_(fds.size == 0);
	return reply;
}
template<Type Request> typename Request::Reply requestFD(Request request, buffer<int>& fds, const ref<byte> data={}) {
	buffer<byte> output;
	typename Request::Reply reply = ::request(request, output, fds, data);
	assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
	return reply;
}
template<Type Request> typename Request::Reply request(Request request, const ref<byte> data={}, int fd=-1) {
	buffer<byte> output;
	typename Request::Reply reply = ::request(request, output, data, fd);
	assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
	return reply;
}

int main() {
	// -- Display
	{string path = "/tmp/.X11-unix/X0"_;
		struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; mref<char>(addr.path,path.size).copy(path);
		check(connect(socketFD, (const sockaddr*)&addr, 2+path.size));
	}
	{
		BinaryData s (readFile(".Xauthority",home()), true);
		string name, data;
		uint16 family unused = s.read();
		{uint16 length = s.read(); string host unused = s.read<byte>(length); }
		{uint16 length = s.read(); string port unused = s.read<byte>(length); }
		{uint16 length = s.read(); name = s.read<byte>(length); }
		{uint16 length = s.read(); data = s.read<byte>(length); }
		sendRaw(raw(ConnectionSetup{.nameSize=uint16(name.size), .dataSize=uint16(data.size)})+pad(name)+pad(data));
	}
	{ConnectionSetupReply1 unused r=read<ConnectionSetupReply1>(); assert(r.status==1);}
	{ConnectionSetupReply2 r=read<ConnectionSetupReply2>();
		read(align(4,r.vendorLength));
		read<XFormat>(r.numFormats);
		for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>();
			for(int i=0;i<screen.numDepths;i++) { XDepth depth = read<XDepth>();
				if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(depth.numVisualTypes)) {
					if(!visual && depth.depth==32) {
						root = screen.root;
						visual = visualType.id;
					}
				}
			}
		}
		id=r.ridBase;
		assert_(visual);
	}
	{auto r = request(QueryExtension{.length="DRI3"_.size, .size=uint16(2+align(4,"DRI3"_.size)/4)}, "DRI3"_); DRI3::EXT=r.major; assert_(DRI3::EXT); }
	{auto r = request(QueryExtension{.length="Present"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "Present"_); Present::EXT=r.major; assert_(Present::EXT); }

	// -- Window
	/// Window size
	uint width = 512, height = width;
	/// Associated window resource (relative to resource ID base Display::id)
	enum Resource { XWindow, Colormap, PresentEvent, Pixmap };
	/// GPU device
	int drmDevice = 0;
	struct gbm_device* gbmDevice = 0;
	EGLDisplay eglDevice = 0;
	EGLConfig eglConfig = 0;
	EGLContext eglContext = 0;
	/// GBM/EGL surface
	struct gbm_surface* gbmSurface = 0;
	EGLSurface eglSurface = 0;
	struct gbm_bo* bo = 0;

	// -- Creates X window
	send(CreateColormap{ .colormap=id+Colormap, .window=root, .visual=visual});
	send(CreateWindow{.id=id+XWindow, .parent=root, .width=uint16(width), .height=uint16(height), .visual=visual, .colormap=id+Colormap});
	send(Present::SelectInput{.window=id+XWindow, .eid=id+PresentEvent});
	send(MapWindow{.id=id});

	// -- Opens GPU device
	request(DRI3::QueryVersion());
	drmDevice = ({buffer<int> fds; requestFD(DRI3::Open{.drawable=id+XWindow}, fds); fds[0]; });
	gbmDevice = gbm_create_device(drmDevice);
	eglDevice = eglGetDisplay((EGLNativeDisplayType)gbmDevice);
	assert_(eglDevice);
	EGLint major, minor; eglInitialize(eglDevice, &major, &minor);
	eglBindAPI(EGL_OPENGL_API);
	EGLint n;
	eglChooseConfig(eglDevice, (EGLint[]){EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE}, &eglConfig, 1, &n);
	assert_(eglConfig);
	eglContext = eglCreateContext(eglDevice, eglConfig, 0, (EGLint[]){EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
																	  EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE});
	assert_(eglContext);
	gbmSurface = gbm_surface_create(gbmDevice, width, height, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	eglSurface = eglCreateWindowSurface(eglDevice, eglConfig, (EGLNativeWindowType)gbmSurface, 0);
	assert_(eglSurface);
	eglMakeCurrent(eglDevice, eglSurface, eglSurface, eglContext);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	assert_(gbm_surface_has_free_buffers(gbmSurface));
	glFinish();
	assert_( eglSwapBuffers(eglDevice, eglSurface) );
	bo = gbm_surface_lock_front_buffer(gbmSurface);
	int fd = 0; drmPrimeHandleToFD(drmDevice, gbm_bo_get_handle(bo).u32, 0, &fd);
	//byte4* pixels = (byte4*)mmap(0, height*width*4, PROT_READ|PROT_WRITE, MAP_SHARED, dmabuf->fd, 0); log(pixels); mref<byte4>(pixels, height*width).clear(0); // FIXME: mmap returns -1 (PERM)
	send(DRI3::PixmapFromBuffer{.pixmap=id+Pixmap,.drawable=id+XWindow,.bufferSize=height*width*4,.width=uint16(width),.height=uint16(height),.stride=uint16(width*4)}, fd);
	send(Present::Pixmap{.window=id+XWindow, .pixmap=id+Pixmap});
	sleep(1);
}
