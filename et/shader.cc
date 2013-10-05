#include "shader.h"
#include "image.h"
//#include "jpeg.h"
//#include "tga.h"

unique<GLTexture> upload(const ref<byte>& file) {
    Image image = decodeImage(file);
    assert_(image);
    // Fill transparent pixels for correct alpha linear blend color (using nearest opaque pixels)
    byte4* data=image.data; int w=image.width; int h=image.height;
    for(int y: range(h)) for(int x: range(w)) {
        byte4& t = data[y*w+x];
        if(t.a>=0x80) continue; // Only fills pixel under alphaTest threshold
        int alphaMax=0x80;
        for(int dy=-1;dy<1;dy++) for(int dx=-1;dx<1;dx++) {
            byte4& s = data[(y+dy+h)%h*w+(x+dx+w)%w]; // Assumes wrapping coordinates
            if(s.a > alphaMax) { alphaMax=s.a; t=byte4(s.b,s.g,s.r, t.a); } // FIXME: alpha-weighted blend of neighbours
        }
    }
    return unique<GLTexture>(image, (image.alpha?RGBA:RGB8)|Mipmap|Bilinear|Anisotropic);
}

FILE(shader)

GLShader& Shader::bind() {
    if(!program) {
        static map<String,unique<GLShader>> programs;
        array<String> stages; stages << copy(type);
        for(const Texture& texture: *this) stages << copy(texture.type);
        stages << copy(final);
        String id = join(stages,";"_);
        if(!programs.contains(id)) programs.insert(copy(id), unique<GLShader>(shader(), toRefs(stages)));
        program = programs.at(id).pointer;
    }
    program->bind();
    return *program;
}
