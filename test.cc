#include "thread.h"
#include "string.h"
#include "time.h"
#include "math.h"

struct Script { Script() {
#if 1
        Date start(10,April,2013), now = currentTime(), end(21,August,2013);
        uint since = now - start, until = end - now, total = since+until;
        log("Elapsed:",since/60/60/24,"days", "["_+str(100.0*since/total)+"%]"_, "\tRemaining:",until/60/60/24,"days", "["_+str(100.0*until/total)+"%]"_,"\tTotal:",total/60/60/24,"days");
        log("1/2:",Date(start + 1*total/2), "2/3:",Date(start + 2*total/3), "3/4:",Date(start + 3*total/4));
#endif
#if 1
        const int steps=8; log(apply(range(steps*log2(512)-45,steps*log2(512)), [](double i){ return dec(round(pow(2,(double)i/steps)),3); } )<<dec(506),',');
#endif
} } script;
