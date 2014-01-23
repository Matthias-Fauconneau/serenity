#include "file.h"
#include "widget.h"

const int keyCount = 85;
float stretch(int m) { return -exp((-33 - m)/12.) + exp((m - 150)/12.); }

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
    int2 sizeHint() { return int2(keyCount*12, -236); }
    void render(int2 position, int2 size) {
        float minimumOffset = -1./4;
        float maximumOffset = 1./4;
        for(int key: range(keyCount)) {
            int x0 = position.x + key * size.x / keyCount;
            int x1 = position.x + (key+1) * size.x / keyCount;

#if 0
            float target = stretch(21+key)*12;
            float offset = offsets[key];
            int y0 = position.y + size.y * (maximumOffset-target) / (maximumOffset-minimumOffset);
            int y1 = position.y + size.y * (maximumOffset-offset) / (maximumOffset-minimumOffset);
            fill(x0,y0<y1?y0:y1,x1,y0<y1?y1:y0, offset>target ? vec4(1,0,0,1) : vec4(0,0,1,1));
#else
            float p0 = stretch(21+key)*12;
            int y0 = position.y + size.y * (maximumOffset-p0) / (maximumOffset-minimumOffset);

            float offset = offsets[key]-p0;
            float deviation = sqrt(variances[key]);
            float sign = ::sign(offset) ? : 1;

            // High confidence between zero and max(0, |offset|-deviation)
            float p1 = max(0.f, abs(offset)-deviation);
            int y1 = position.y + size.y * (maximumOffset-sign*p1-p0) / (maximumOffset-minimumOffset);
            fill(x0,y0<y1?y0:y1,x1,y0<y1?y1:y0, sign*p1>0 ? vec4(1,0,0,1) : vec4(0,0,1,1));

            // Mid confidence between max(0,|offset|-deviation) and |offset|
            float p2 = abs(offset);
            int y2 = position.y + size.y * (maximumOffset-sign*p2-p0) / (maximumOffset-minimumOffset);
            fill(x0,y1<y2?y1:y2,x1,y1<y2?y2:y1, sign*p2>0 ? vec4(3./4,0,0,1) : vec4(0,0,3./4,1));

            // Low confidence between |offset| and |offset|+deviation
            float p3 = abs(offset)+deviation;
            int y3 = position.y + size.y * (maximumOffset-sign*p3-p0) / (maximumOffset-minimumOffset);
            fill(x0,y2<y3?y2:y3,x1,y2<y3?y3:y2, sign*p3>0 ? vec4(1./2,0,0,1) : vec4(0,0,1./2,1));

            // Low confidence between min(|offset|-deviation, 0) and zero
            float p4 = min(0.f, abs(offset)-deviation);
            int y4 = position.y + size.y * (maximumOffset-sign*p4-p0) / (maximumOffset-minimumOffset);
            fill(x0,y0<y4?y0:y4,x1,y0<y4?y4:y0, sign*p4>0 ? vec4(1./2,0,0,1) : vec4(0,0,1./2,1));
#endif
        }
    }
};
