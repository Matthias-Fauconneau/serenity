#include "thread.h"
#include "data.h"

struct Test {
    Test() {
        Folder folder("teapot",home());
        String templateConfiguration = readFile("template.json",folder);
        for(int sIndex: range(-16,16 +1)) for(int tIndex: range(-16,16 +1)) {
            array<char> instance {templateConfiguration.size};
            TextData s (templateConfiguration);
            instance.append( s.whileNot("t+") );
            s.skip("t+");
            const float t0 = s.decimal();
            instance.append(str(t0+1*tIndex));
            instance.append( s.whileNot("s+") );
            s.skip("s+");
            const float s0 = s.decimal();
            instance.append(str(s0+1*sIndex));
            instance.append( s.whileNot("%") );
            s.skip("%");
            instance.append(str(sIndex)+","+str(tIndex));
            instance.append(s.untilEnd());
            writeFile("st="+str(sIndex)+","+str(tIndex)+".json",instance,Folder("configurations",folder),true);
        }
    }
} app;
