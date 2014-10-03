#include "file.h"
#include "widget.h"
#include "graphics.h"

const int keyCount = 85;
float stretch(int m) { return -exp((-54 - m)/12.) + exp((m - 129)/12.); }

struct OffsetPlot : Widget {
    float offsets[keyCount] = {};
    float variances[keyCount] = {};
    OffsetPlot() {
      if(!existsFile("offsets.profile"_,config())) return;
      TextData s = readFile("offsets.profile"_,config());
      for(uint i: range(keyCount)) { offsets[i] = clip(-1./2, s.decimal()/100, 1./2); s.skip(" "_); variances[i] = sq(s.decimal()/100); s.skip("\n"_); }
    }
    ~OffsetPlot() {
        String s;
        for(uint i: range(keyCount)) s << str(offsets[i]*100) << " "_ << str(sqrt(variances[i])*100) << "\n"_;
        writeFile("offsets.profile"_, s, config());
    }
    void reset() { mref<float>(offsets).clear(); mref<float>(variances).clear(); }
    int2 sizeHint(int2) const override { return int2(keyCount*12, -236); }
    Graphics graphics(int2 size) const override {
        Graphics graphics;
        float minimumOffset = -1./4;
        float maximumOffset = 1./4;
        for(int key: range(keyCount)) {
            int x0 = key * size.x / keyCount;
            int x1 = (key+1) * size.x / keyCount;

            float p0 = stretch(key)*12;
            int y0 = size.y * (maximumOffset-p0) / (maximumOffset-minimumOffset);

            float offset = offsets[key]-p0;
            float deviation = sqrt(variances[key]);
            float sign = ::sign(offset) ? : 1;

            // High confidence between zero and max(0, |offset|-deviation)
            float p1 = max(0.f, abs(offset)-deviation);
            int y1 = size.y * (maximumOffset-sign*p1-p0) / (maximumOffset-minimumOffset);
            graphics.fills << Fill{vec2(x0, y0<y1?y0:y1), vec2(x1, y0<y1?y1:y0), sign*p1>0 ? red : blue, 1};

            // Mid confidence between max(0,|offset|-deviation) and |offset|
            float p2 = abs(offset);
            int y2 = size.y * (maximumOffset-sign*p2-p0) / (maximumOffset-minimumOffset);
            graphics.fills << Fill{vec2(x0, y1<y2?y1:y2), vec2(x1, y1<y2?y2:y1), 3.f/4*(sign*p2>0 ? red : blue), 1};

            // Low confidence between |offset| and |offset|+deviation
            float p3 = abs(offset)+deviation;
            int y3 = size.y * (maximumOffset-sign*p3-p0) / (maximumOffset-minimumOffset);
            graphics.fills << Fill{vec2(x0, y2<y3?y2:y3), vec2(x1, y2<y3?y3:y2), 1.f/2*(sign*p3>0 ? red : blue), 1};

            // Low confidence between min(|offset|-deviation, 0) and zero
            float p4 = min(0.f, abs(offset)-deviation);
            int y4 = size.y * (maximumOffset-sign*p4-p0) / (maximumOffset-minimumOffset);
            graphics.fills << Fill{vec2(x0, y0<y4?y0:y4), vec2(x1, y0<y4?y4:y0), 1.f/2*(sign*p4>0 ? red : blue), 1};
        }
        return graphics;
    }
};
