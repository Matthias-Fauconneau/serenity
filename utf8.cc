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

string utf8(uint c) {
    string utf8;
    /**/  if(c<(1<<7)) utf8 << c;
    else if(c<(1<<(7+6))) utf8 << (0b11000000|(c>>6)) << (0b10000000|(c&0b111111));
    else if(c<(1<<(7+6+6))) utf8 << (0b11100000|(c>>12)) << (0b10000000|((c>>6)&0b111111)) << (0b10000000|(c&0b111111));
    else assert(0);
    return utf8;
}
