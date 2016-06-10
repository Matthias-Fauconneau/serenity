#include "fret.h"
#include "text.h"
inline double pow(double x, double y) { return __builtin_pow(x,y); } // math.h

shared<Graphics> Fret::graphics(vec2 size) {
    shared<Graphics> graphics;
    const uint stringCount = 6;
    for(uint string=0; string<stringCount; string++) {
     graphics->fills.append(vec2(0, (string+1./2)/stringCount*size.y), vec2(size.x, 1), black, 1.f/2);
    }
    const int fretCount = 16;
    const float max = 1-pow(2,-16/12.); // 0-16
    for(int fret=1; fret<fretCount; fret++) {
     const float x0 = (1-pow(2,-(fret-1)/12.))/max*size.x;
     const float x1 = (1-pow(2,-(fret-0)/12.))/max*size.x;
     graphics->fills.append(vec2(x1, 0), vec2(1, size.y));
     if(ref<int>{3,5,7,9}.contains(fret%12)) {
      const float y0 = (2+1./2)/stringCount*size.y;
      const float y1 = (3+1./2)/stringCount*size.y;
      graphics->graphics.insertMulti(vec2(x0, y0), Text("•",y1-y0,1./2,1./2,0,"DejaVuSans"_,true,1,1).graphics(vec2(x1-x0, y1-y0)));
      //uint index = font->font(size).index(code);
      //system.glyphs.append(origin, size, *font, code, index, black, opacity);
     } else if(fret%12 == 0) {
      {
       const float y0 = 1./stringCount*size.y;
       const float y1 = 2./stringCount*size.y;
       graphics->graphics.insertMulti(vec2(x0, y0), Text("•",y1-y0,1./2,1./2,0,"DejaVuSans"_,true,1,1).graphics(vec2(x1-x0, y1-y0)));
      }
      {
       const float y0 = 4./stringCount*size.y;
       const float y1 = 5./stringCount*size.y;
       graphics->graphics.insertMulti(vec2(x0, y0), Text("•",y1-y0,1./2,1./2,0,"DejaVuSans"_,true,1,1).graphics(vec2(x1-x0, y1-y0)));
      }
     }
     for(Sign sign: measure.values) {
      assert_(sign.note.fret, sign.note.fret, sign.note.string);
      if(sign.note.fret != fret) continue;
      if(active.values.contains(sign)) continue;
      int string = sign.note.string;
      const float y0 = float(string)/stringCount*size.y;
      const float y1 = float(string+1)/stringCount*size.y;
      graphics->graphics.insertMulti(vec2(x0, y0), Text(str(sign),y1-y0,1./2,1./2,0,"DejaVuSans"_,true,1,0).graphics(vec2(x1-x0, y1-y0)));
      //uint index = font->font(size).index(code);
      //system.glyphs.append(origin, size, *font, code, index, black, opacity);
     }
     for(Sign sign: active.values) {
      if(sign.note.fret != fret) continue;
      int string = sign.note.string;
      const float y0 = float(string)/stringCount*size.y;
      const float y1 = float(string+1)/stringCount*size.y;
      graphics->graphics.insertMulti(vec2(x0, y0), Text(str(sign),y1-y0,black,1,0,"DejaVuSans"_,true,1,0).graphics(vec2(x1-x0, y1-y0)));
      //uint index = font->font(size).index(code);
      //system.glyphs.append(origin, size, *font, code, index, black, opacity);
     }
    }
    return graphics;
}
