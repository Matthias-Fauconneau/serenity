#include "utf8.h"

/// utf8_iterator
uint utf8_iterator::operator* () const {
    byte code = pointer[0];
    /**/  if((code&0b10000000)==0b00000000) return code;
    else if((code&0b11100000)==0b11000000) return(code&0b11111)<<6  |(pointer[1]&0b111111);
    else if((code&0b11110000)==0b11100000) return(code&0b01111)<<12|(pointer[1]&0b111111)<<6  |(pointer[2]&0b111111);
    else if((code&0b11111000)==0b11110000) return(code&0b00111)<<18|(pointer[1]&0b111111)<<12|(pointer[2]&0b111111)<<6|(pointer[3]&0b111111);
    else return code; //Windows-1252
}
const utf8_iterator& utf8_iterator::operator++() {
    byte code = *pointer;
    /**/  if((code&0b10000000)==0b00000000) pointer+=1;
    else if((code&0b11100000)==0b11000000) pointer+=2;
    else if((code&0b11110000)==0b11100000) pointer+=3;
    else if((code&0b11111000)==0b11110000) pointer+=4;
    else pointer+=1; //Windows-1252
    return *this;
}

String utf8(uint c) {
	array<char> utf8;
	/**/  if(c<(1<<7)) utf8.append(c);
	else if(c<(1<<(7+6))) {
		utf8.append(0b11000000|(c>>6));
		utf8.append(0b10000000|(c&0b111111));
	}
	else if(c<(1<<(7+6+6))) {
		utf8.append(0b11100000|(c>>12));
		utf8.append(0b10000000|((c>>6)&0b111111));
		utf8.append(0b10000000|(c&0b111111));
	}
    else assert(0);
	return move(utf8);
}

generic array<T> toUCS(string utf8) {
 array<T> ucs(utf8.size);
	for(utf8_iterator it=utf8.begin(); it!=utf8_iterator(utf8.end());++it) ucs.append( *it );
    return ucs;
}
array<uint16> toUCS2(string utf8) { return toUCS<uint16>(utf8); }
array<uint32> toUCS4(string utf8) { return toUCS<uint32>(utf8); }

generic String toUTF8(ref<T> ucs) {
 array<char> utf8(ucs.size);
	for(uint c: ucs) utf8.append( ::utf8(c) );
	return move(utf8);
}
String toUTF8(ref<uint16> ucs) { return toUTF8<uint16>(ucs); }
String toUTF8(ref<uint32> ucs) { return toUTF8<uint32>(ucs); }
