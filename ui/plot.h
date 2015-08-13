#pragma once
#include "widget.h"
#include "map.h"
#include "data.h"

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

struct Fit { float a, b; };

struct Plot : virtual Widget {
 enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
 Plot(string title=""_, bool plotLines=true, LegendPosition legendPosition=TopRight)
  : name(copyRef(title)), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition) {}
 String title() override { return copyRef(name); }
 vec2 sizeHint(vec2) override;
 shared<Graphics> graphics(vec2 size) override;

 String name, xlabel, ylabel;
 bool log[2] = {false, false};
 map<NaturalString, map<float,float>> dataSets;
 map<NaturalString, array<Fit>> fits;
 bool plotPoints, plotLines, plotBandsX = false, plotBandsY = false, plotCircles = false;
 LegendPosition legendPosition;
 vec2 min = 0, max = 0;
};
