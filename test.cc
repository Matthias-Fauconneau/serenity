#include "thread.h"
#include "string.h"
#include "time.h"
#include "math.h"

struct Script { Script() {
#if 0
        Date start(10,April,2013), now = currentTime(), end(21,August,2013);
        uint since = now - start, until = end - now, total = since+until;
        log("Elapsed:",since/60/60/24,"days", "["_+str(100.0*since/total)+"%]"_, "\tRemaining:",until/60/60/24,"days", "["_+str(100.0*until/total)+"%]"_,"\tTotal:",total/60/60/24,"days");
        log("1/2:",Date(start + 1*total/2), "2/3:",Date(start + 2*total/3), "3/4:",Date(start + 3*total/4));
#endif
#if 1
        log(apply(range(4*log2(512)-15,4*log2(512)+1), [](double i){ return dec(round(pow(2,i/4.)),3); } ),',');
#endif
} } script;
