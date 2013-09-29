#include "material.h"
#include "data.h"

map<String,unique<Shader> > parseMaterialFile(string data) {
    map<String,unique<Shader>> shaders;
    int comment=0;
    for(TextData s(data); s; s.skip()) {
        s.skip();
        if(s.match("//"_)) { s.until('\n'); continue; }
        if(s.match("/*"_)) { comment++; continue; }
        if(s.match("*/"_)) { comment--; continue; }
        if(comment) continue;
        string name = s.identifier("/_"_);
        assert(name);
        Shader& shader = shaders[name]; shader.name=String(name); shader.clear(); shader.properties.clear(); shader.program=0;
        uint index = s.index;
        int nest=0,comment=0; Texture* current=0; //bool alphaBlend=false;
        while(s) {
            s.skip();
            if(s.match("//"_)) { s.until('\n'); continue; }
            if(s.match("/*"_)) { comment++; continue; }
            if(s.match("*/"_)) { comment--; continue; }
            if(comment) continue;
            if(s.match('{')) { nest++; continue; }
            if(s.match('}')) { nest--; if(nest==0) break; if(nest==1) current=0; continue; }
            string key = s.identifier("_"_); s.whileAny(" "_);
            string value = s.untilAny("\r\n"_);
            array<string> args = split(value); //l.replace(QRegExp("[,()]")," ").simplified().split(' ').mid(1);
            args.filter([](const string& s){return s=="("_||s==")"_;});
            /**/ if(key=="map"_ || key=="clampmap"_ || key=="lightmap"_) { //diffusemap
                if(!current) { shader.append(Texture()); current=&shader.last(); }
                current->path = String(args[0]);
                if(key=="clampmap"_) current->clamp=true;
                if(key=="lightmap"_) assert_(current->path=="$lightmap"_);
            }
            else if(split("implicitMap implicitBlend implicitMask"_).contains(key)) {
                string map = args[0];
                if(map=="-"_) map = name;
                shader.append(Texture(map)); current=&shader.last();
                if(key=="implicitMask"_||key=="implicitBlend"_) {
                    current->alpha=true;
                    current->type<<" sfactor_src_alpha blend_one_minus_src_alpha"_;
                    shader.blendAlpha=true;
                }
                shader.append(Texture("$lightmap"_));
            }
            else if(key=="animMap"_) { shader.append(Texture(args[1])); current=&shader.last(); }
            else if(key=="polygonOffset"_ || key=="polygonoffset"_) shader.polygonOffset=true;
            else if(key=="surfaceParm"_||key=="surfaceparm"_) { /*if(args[0]=="trans"_||args[0]=="alphashadow"_) alphaBlend=true;*/ }
            else if(key=="sort"_) { /*if(key=="additive"_) alphaBlend=true;*/ }
            else if(key=="skyparms"_) shader.properties[String("skyparms"_)] = String(args[1]);
            else if(key=="fogparms"_) shader.properties.insert(String("fogparms"_), String(args[3])); //FIXME: fog color
            else if(key=="q3map_sun"_||key=="q3map_sunExt"_) shader.properties[String("q3map_sun"_)]=String(value);
            else if(key=="sunshader"_) shader.properties[String("sunshader"_)]=String(value); //<red> <green> <blue> <intensity> <degrees> <elevation>
            else if(split("nocompress nopicmip nomipmaps nofog fog waterfogvars cull tcGen tcgen depthWrite depthwrite depthFunc detail qer_editorimage qer_trans q3map_globaltexture q3map_lightimage q3map_surfacelight q3map_clipModel q3map_shadeangle"_).contains(key)) {} // Ignored or default
            else if(!current) { /*error("No current texture for",key,args); Happens on explicit $lightmap blendFunc multiply*/ continue; }
            else if(key=="alphaFunc"_||key=="alphafunc"_) { current->alpha=true; current->type<<" sfactor_src_alpha blend_one_minus_src_alpha"_; shader.blendAlpha=true; }
            else if(key=="vertexColor"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; }
            else if(key=="alphaGen"_||key=="alphagen"_) { if(args[0]=="vertex"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; } }
            else if(key=="tcMod"_||key=="tcmod"_) {
                /**/  if(args[0]=="scale"_ || args[0]=="Scale"_) {
                    current->tcMod = mat3x2(toDecimal(args[1]),0, 0,toDecimal(args[2]), 0,0) * current->tcMod;
                }
                else if(args[0]=="transform"_) {
                    array<double> m = apply(args.slice(1),toDecimal); // m00 m01 m10 m11 dx dy
                    current->tcMod = mat3x2(m[0],m[1], m[2],m[3], m[4],m[5]) * current->tcMod;
                }
                else if(args[0]=="rotate"_) {/*TODO*/}
                else if(args[0]=="scroll"_) {/*TODO*/}
                else if(args[0]=="stretch"_) {/*TODO*/}
                else if(args[0]=="turb"_) {/*TODO*/}
                else error(key, args);
            }
            else if(key=="rgbGen"_||key=="rgbgen"_) {
                if(args[0]=="const"_) current->rgbScale=vec3(toDecimal(args[1]),toDecimal(args[2]),toDecimal(args[3]));
                if(args[0]=="wave"_) current->rgbScale=vec3(toDecimal(args[2]),toDecimal(args[3]),toDecimal(args[4]));
                //else
            }
            else if(key=="blend"_||key=="blendFunc"_||key=="blendfunc"_||key=="blenfunc"_) {
                String sfactor,dfactor; //FIXME: flags
                if(args[0]=="add"_) { sfactor=String("one"_); dfactor=String("one"_); }
                else if(args[0]=="blend"_) { sfactor=String("src_alpha"_); dfactor=String("one_minus_src_alpha"_); }
                else if(args[0]=="filter"_) { sfactor=String("zero"_); dfactor=String("src_color"_); }
                //else if(args[0]=="decal"_ || args[0]=="detail"_) { sfactor=String("zero"_); dfactor=String("src_color_plus_src_color"_); }
                else { assert_(args.size==2, args); sfactor=toLower(args[0].slice(3)); dfactor=toLower(args[1].slice(3)); }
                if(sfactor=="dst_color"_ && dfactor=="zero"_) { sfactor=String("zero"_); dfactor=String("src_color"_); } // dst·src + 0·dst = src·dst
                if(sfactor=="dst_color"_ && dfactor=="one"_) { sfactor=String("zero"_); dfactor=String("one_plus_src_color"_); } // dst·src + 1·dst = (1+src)·dst
                if(sfactor=="dst_color"_ && dfactor=="src_color"_) { sfactor=String("zero"_); dfactor=String("src_color_plus_src_color"_); } // dst·src + src·dst = (src+src)·dst
                if(sfactor=="one_minus_dst_color"_ && dfactor=="one_minus_src_color"_) { sfactor=String("one"_); dfactor=String("one_minus_src_color_minus_src_color"_); } // (1-dst)·src + src·(1-dst) = src+(1-src-src)·dst
                //assert_(!shader.blendAlpha && !shader.blendColor, name, sfactor, dfactor);

                if(find(sfactor,"alpha"_)||find(dfactor,"alpha"_)) current->alpha=true; //FIXME: assert | or vertex alpha ?
                current->type<<" sfactor_"_<<sfactor;
                if(shader.size>1) current->type << " dfactor_"_<<dfactor; // Multitexture shader
                else { // Blend shader
                    assert_(sfactor=="zero"_||sfactor=="one"_||sfactor=="src_alpha"_||sfactor=="src_color"_, name, sfactor, dfactor);
                    current->type<<" blend_"_+dfactor;
                    if(dfactor=="one"_||dfactor=="src_alpha"_||dfactor=="one_minus_src_alpha"_) {
                        shader.blendAlpha=true; // Multiply destination by alpha and add source
                    }
                    else if(sfactor=="zero"_) {
                        assert_(find(dfactor,"src_color"_), name, sfactor, dfactor);
                        shader.blendColor=true; // Multiply destination by source
                    }
                    else if(dfactor=="one_minus_src_color"_||dfactor=="one_minus_src_color_minus_src_color"_) { // Approximate using average as alpha
                        shader.blendAlpha=true;
                        current->type<<"to_alpha"_; // blend_one_minus_src_color_to_alpha
                    } else if(dfactor=="zero"_) {} // Replace destination with source (opaque)
                    else error(name, sfactor, dfactor);
                }
            }
            else error("Unknown key",key, args);
        }
        if(shader.size>8) error("Too many textures for shader");
        shader.source=String(s.slice(index,s.index-index));
    }
    return shaders;
}
