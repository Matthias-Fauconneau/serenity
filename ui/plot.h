#pragma once
#include "widget.h"
#include "map.h"
#include "data.h"
#include "math.h"

typedef String NaturalString;
inline bool operator <(const NaturalString& a, const NaturalString& b) {
 TextData A (a), B (b);
 for(;;) {
  { // Numerical
   string a = A.whileInteger();
   string b = B.whileInteger();
   if(a && b && a != b) return parseInteger(a) < parseInteger(b);
  }
  if(!A && B) return true;
  if(!B) return false;
  { // Lexical
   char a = A.next();
   char b = B.next();
   if(a != b) return a < b;
  }
 }
}

struct Plot : virtual Widget {
 String name, xlabel, ylabel;
 bool log[2] = {false, false};
 map<NaturalString, map<float,float>> dataSets;
 bool plotPoints, plotLines, plotBandsY = false;
 enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight } legendPosition;
 vec2 min = inff, max = 0;

 Plot(string title=""_, bool plotLines=true, LegendPosition legendPosition=TopRight) : name(copyRef(title)), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition) {}

 String title() const override { return copyRef(name); }
 vec2 sizeHint(vec2) override;
 void render(RenderTarget2D& target, vec2 offset=0, vec2 size=0) override;
};
