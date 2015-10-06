#include "color.h"
#include "window.h"
#include "interface.h"
#include "time.h"

Image randomHueImage(float chroma=131/*131,136,179*/, float lightness=/*53*/87 /*32,53,87*/) {
 Image image (512);
 //Random random;
 //for(byte4& sRGB: image) {
 for(int y : range(image.size.y)) {
  for(int x : range(image.size.x)) {
   vec2 v (y-image.size.y/2.f, x-image.size.x/2.f);
   float c = length(v) > image.size.x/2 ? 0 : chroma;
   //float hue = 2*PI*random();
   float hue = atan(v.x, v.y);
   bgr3f color = clamp(bgr3f(0), LChuvtoBGR(lightness, c, hue), bgr3f(1));
   assert_(isNumber(color.b) && isNumber(color.g) && isNumber(color.r), color, v, lightness, c, hue);
   bgr3i linear = bgr3i(/*round*/(float(0xFFF)*color));
   extern uint8 sRGB_forward[0x1000];
   /*sRGB*/image(x,y) = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
  }
 }
 return image.size!=int2(512) ? resize(512, image) : ::move(image);
}

#if 0
ImageView view = randomHueImage();
unique<Window> theWindow = ::window(&view, int2(512));
#else
struct Test : ImageView {
 unique<Window> window = ::window(this, int2(512));
 float lmin = 30/*32*/, lmax=100/*87*/; //0-100
 float lightness = lmin;
 float step = 1;
 shared<Graphics> graphics(vec2 size) override {
  window->render();
  if(step == 1 && lightness > lmax) step = -1, lightness=lmax;
  if(step == -1 && lightness < lmin) step = 1, lightness=lmin;
  image = randomHueImage(179, lightness);
  lightness += step;
  return ImageView::graphics(size);
 }
} test;
#endif
