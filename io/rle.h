#pragma once
#include "memory.h"

static buffer<byte> decodeRunLength(const ref<byte> source) {
    array<byte> buffer (source.size);
    Data s (source);
    for(;;) {
        assert_(s);
        uint8 code = s.next();
        if(code < 128) buffer << s.read(code+1);
        else if(code != 128) {
            byte value = s.next();
            uint size = 257-code;
            buffer.reserve(buffer.size+size);
            for(uint unused i: range(size)) buffer << value;
        }
        else break;
    }
    return move(buffer);
}
