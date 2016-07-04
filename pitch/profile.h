#include "file.h"
#include "widget.h"
#include "graphics.h"
#include "data.h"

// \file ?
generic T sign(T x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
inline double exp(double x) { return __builtin_exp(x); } // math.h

static constexpr int keyCount = 85;
float stretch(int m) { return -exp((-54 - m)/12.) + exp((m - 129)/12.); }

static Folder config(".config", home(), true);

struct OffsetPlot : Widget {
    float offsets[keyCount] = {};
    float variances[keyCount] = {};
    OffsetPlot() {
      if(!existsFile("offsets.profile",config)) return;
      auto data = readFile("offsets.profile", config);
      TextData s (data);
	  for(uint i: range(keyCount)) { offsets[i] = clamp(-1./2, s.decimal()/100, 1./2); s.skip(' '); variances[i] = sq(s.decimal()/100); s.skip('\n'); }
    }
    ~OffsetPlot() {
		array<char> s;
		for(uint i: range(keyCount)) s.append(str(offsets[i]*100)+' '+str(sqrt(variances[i])*100)+'\n');
        writeFile("offsets.profile", s, config, true);
    }
    void reset() { mref<float>(offsets).clear(); mref<float>(variances).clear(); }
	vec2 sizeHint(vec2) override { return vec2(-keyCount*18, 768); }
	shared<Graphics> graphics(vec2 size) override {
		shared<Graphics> graphics;
        //float minimumOffset = -1./4;
        //float maximumOffset = 1./4;
	float minimumOffset = -1./2;
        float maximumOffset = 1./2;
        for(int key: range(keyCount)) {
            int x0 = key * size.x / keyCount;
            int x1 = (key+1) * size.x / keyCount;
            int dx = x1-x0;

            float p0 = stretch(key) * 12;
            int y0 = size.y * (maximumOffset-p0) / (maximumOffset-minimumOffset);

            float offset = offsets[key]-p0;
            float deviation = sqrt(variances[key]);
            float sign = ::sign(offset) ? : 1;

            // High confidence between zero and max(0, |offset|-deviation)
            float p1 = max(0.f, abs(offset)-deviation);
            int y1 = size.y * (maximumOffset-sign*p1-p0) / (maximumOffset-minimumOffset);
			graphics->fills.append(vec2(x0, min(y0,y1)), vec2(dx, abs(y0-y1)), sign*p1>0 ? red : blue, 1.f);

            // Mid confidence between max(0,|offset|-deviation) and |offset|
            float p2 = abs(offset);
            int y2 = size.y * (maximumOffset-sign*p2-p0) / (maximumOffset-minimumOffset);
			graphics->fills.append(vec2(x0, min(y1,y2)), vec2(dx, abs(y1-y2)), 3.f/4*(sign*p2>0 ? red : blue), 1.f);

            // Low confidence between |offset| and |offset|+deviation
            float p3 = abs(offset)+deviation;
            int y3 = size.y * (maximumOffset-sign*p3-p0) / (maximumOffset-minimumOffset);
			graphics->fills.append(vec2(x0, min(y2,y3)), vec2(dx, abs(y2-y3)), 1.f/2*(sign*p3>0 ? red : blue), 1.f);

            // Low confidence between min(|offset|-deviation, 0) and zero
            float p4 = min(0.f, abs(offset)-deviation);
            int y4 = size.y * (maximumOffset-sign*p4-p0) / (maximumOffset-minimumOffset);
			graphics->fills.append(vec2(x0, min(y4,y0)), vec2(dx, abs(y4-y0)), 1.f/2*(sign*p4>0 ? red : blue), 1.f);
        }
        return graphics;
    }
};
