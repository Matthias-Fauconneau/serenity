#include "string.h"
#include "file.h"
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

int main(int argc, const char** argv) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, 0, 0);
    EGLConfig config; EGLint matchingConfigurationCount;
    {int attributes[] ={EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
        eglChooseConfig(display, attributes, &config, 1, &matchingConfigurationCount);}
    assert(matchingConfigurationCount == 1);
    {int attributes[]={EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        EGLContext context = eglCreateContext(display, config, 0, attributes);
        eglMakeCurrent(display, 0, 0, context);}

    if(argc!=2) { error("Usage: glsl shader.{vert|frag}"_); return -1; }
    ref<byte> name = str(argv[1]);
    string source = readFile(name,cwd());
    uint shader = glCreateShader(endsWith(name,".vert"_)?GL_VERTEX_SHADER:endsWith(name,".frag"_)?GL_FRAGMENT_SHADER:(error(""),0));
    const char* data = source.data(); const int size = source.size();
    glShaderSource(shader, 1, &data, &size);
    glCompileShader(shader);
    int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
    if(!status) {
        log(name+":1:1: error: Failed to compile"_);
        int length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        if(length) {
            string buffer (length);
            glGetShaderInfoLog(shader,length,&length,buffer.data());
            buffer.setSize(length);
            log(buffer);
        }
        return -1;
    }
    int binaryLength;
    glUseShader();
    glGetSh
    assert(binaryLength);
    array<byte> binary(binaryLength);
    GLenum binaryFormat;
    glGetProgramBinary(program, binaryLength, 0, &binaryFormat, (void*)&binary);
    binary.size = binaryLength;
    assert(!exists(args[0]));
    int fd = createFile(args[0]);
    write(fd,binary);
    close(fd);
    return 0;
}
