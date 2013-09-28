#include "shader.h"
//#include "jpeg.h"
//#include "tga.h"

Folder data __attribute((init_priority(1000))) = ""_;
map<String,unique<GLTexture>> textures __attribute((init_priority(1000)));
FILE(shader)

void Texture::upload() {
    if(endsWith(path,".tga"_)) path=String(section(path,'.',0,-2));
    if(!textures.contains(path)) {
        if(find(type,"albedo"_) || find(type,"lightmap"_)) {
            Image image;
            {
                Map file;
                if(existsFile(path,data)) file=Map(path,data);
                else if(existsFile(path+".png"_,data)) file=Map(path+".png"_,data);
                else if(existsFile(path+".jpg"_,data)) file=Map(path+".jpg"_,data);
                else if(existsFile(path+".tga"_,data)) file=Map(path+".tga"_,data);
                assert_(file, data.name()+"/"_+path);
                image = decodeImage(file);
                assert_(image, path);
                if(image.alpha && alpha) {
                    byte4* data=image.data; int w=image.width; int h=image.height;
                    for(int y: range(h)) for(int x: range(w)) {
                        byte4& t = data[y*w+x];
                        /*if(alpha)*/ { // Fill transparent pixels for correct alpha linear blend color (using nearest opaque pixels)
                            if(t.a>=0x80) continue; // Only fills pixel under alphaTest threshold
                            int alphaMax=0x80;
                            for(int dy=-1;dy<1;dy++) for(int dx=-1;dx<1;dx++) {
                                byte4& s = data[(y+dy+h)%h*w+(x+dx+w)%w]; // Assumes wrapping coordinates
                                if(s.a > alphaMax) { alphaMax=s.a; t=byte4(s.b,s.g,s.r, t.a); } // FIXME: alpha-weighted blend of neighbours
                            }
                        } /*else { // Asserts opaque
                        assert(t.a==0xFF, path);
                        t.a=0xFF;
                    }*/
                    }
                }
            }
            textures.insert(copy(path),unique<GLTexture>(image, (image.alpha?sRGBA:sRGB8)|Mipmap|Bilinear|Anisotropic|(clamp?Clamp:0)));
        } else error(type); /*{
            QImage image;
            if(QFile::exists(findImage(path+"_cone"))) image=loadImage(path+"_cone");
            else {
                image = loadImage( path );
                if(image.hasAlphaChannel() || !heightMap.isEmpty()) {
                    int w=image.width(); int h=image.height();
                    QRgb* dst=(QRgb*)image.bits();
                    QImage alpha; uchar* src=alpha.bits();
                    if(image.format()==QImage::Format_ARGB32) {
                        alpha = QImage(image.size(),QImage::Format_Indexed8);
                        src=alpha.bits();
                        for(int i=0;i<w*h;i++) src[i]=qAlpha(dst[i]);
                        //qDebug()<<"Preprocessing"<<findImage(path);
                    } else {
                        image=image.convertToFormat(QImage::Format_ARGB32);
                        QImage alpha = loadImage( heightMap );
                        if(alpha.depth()!=8) qWarning()<<heightMap<<": Depth map should have 8bpp";
                        if(image.size()!=alpha.size()) qWarning()<<"Size mismatch: "<<path<<image.size()<<heightMap<<alpha.size();
                        src=alpha.bits();
                        //qDebug()<<"Preprocessing"<<findImage(heightMap);
                    }
                    for(int y=0,i=0;y<h;y++) for(int x=0;x<w;x++,i++) { int i = y*w+x;
                        int depth = inverted?255-src[i]:src[i];
                        float ratio = 1.0;

#define compare(X,Y) ({ \
    int s = src[(Y<0?Y+h:Y>=h?Y-h:Y)*w+(X<0?X+w:X>=w?X-w:X)]; \
    float h2 = ( depth - (inverted?255-s:s) ) / 255.0; \
    if(h2>0) ratio=min(ratio,(sqr(float(X-x)/w)+sqr(float(Y-y)/h))/(h2*h2)); })

                        //scan in outwardly expanding box (allow early stop)
                        for(int radius=1;radius*radius<=ratio*depth*depth*w*h/255/255;radius++) {
                            for(int dy=y-radius+1; dy<y+radius; dy++) { //vertical run //corners processed in horizontal run
                                compare(x-radius,dy); //Left edge
                                compare(x+radius,dy); //Right edge
                            }
                            for(int dx=x-radius;dx<=x+radius;dx++) { //horizontal run
                                compare(dx,y-radius); //Top edge
                                compare(dx,y+radius); //South edge
                            }
                        }
                        ratio = sqrt(ratio);
                        //ratio = sqrt (ratio); //better quantization (cost: 1 multiply in shader)
                        dst[i]=qRgba(qRed(dst[i]),qGreen(dst[i]),ratio*255,inverted?255-src[i]:src[i]);
                    }
                    path=findImage(path).section(".",0,-2)+"_cone.png";
                    if(!QFileInfo(path).dir().exists()) { qWarning()<<"Directory not found"<<path; return; }
                    image.save(path);
                }
            }
            texture->upload(image);
        }
    }*/
    }
    texture = textures.at(path).pointer;
}

GLShader* Shader::bind() {
    if(!program) {
        static map<String,unique<GLShader>> programs;
        array<String> stages; stages << copy(type);
        for(const Texture& texture: *this) stages << copy(texture.type);
        stages << String("forward"_); //FIXME: Forward rendering only
        String id = join(stages,";"_);
        if(!programs.contains(id)) programs.insert(copy(id), unique<GLShader>(shader(), toRefs(stages)));
        program = programs.at(id).pointer;
    }
    program->bind();
    return program;
}
